#include "engine/engine.h"
#include "engine/app.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"
#include "engine/vulkan_buffer.h"
#include "engine/input.h"
#include "engine/platform.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

VkPipeline pipeline;
VkPipelineLayout pipelineLayout;

#include "3rdparty/stb_image.h"

#include "modules/gmath.h"

#include "math.h"

typedef struct{
    vec3 position;
    vec3 normal;
} Vertex;

typedef struct{
    Vertex* items;
    size_t count;
    size_t capacity;
} Vertices;

typedef struct{
    mat4 proj;
    mat4 view;
    mat4 model;
} pushConstants;

pushConstants pcs = {0};

VkBuffer verticesBuffer;
VkDeviceMemory verticesMemory;
VkBuffer indicesBuffer;
VkDeviceMemory indicesMemory;


vec2 rot = {0};
vec3 position = (vec3){0,0,0};

VkImage depthImage;
VkDeviceMemory depthImageMemory;
VkImageView depthImageView;

void destroyDepthImage(){
    if(depthImage == NULL) return;
    vkDestroyImageView(device, depthImageView, NULL);
    vkDestroyImage(device, depthImage, NULL);
    vkFreeMemory(device,depthImageMemory, NULL);
}

bool initDepthImage(){
    if(!createImage(swapchainExtent.width,swapchainExtent.height,VK_FORMAT_D16_UNORM,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&depthImage,&depthImageMemory)) return false;
    if(!createImageView(depthImage, VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, &depthImageView)) return false;

    VkCommandBuffer tempCmd = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depthImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        tempCmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    endSingleTimeCommands(tempCmd);

    return true;
}

typedef struct{
    vec3* items;
    size_t count;
    size_t capacity;
} vec3s;

void wavefrontParse(char* data, Vertices* verticesOut){
    char* current_pos = data;
    while ((current_pos = strchr(current_pos, '\r'))) {
        memmove(current_pos, current_pos + 1, strlen(current_pos + 1) + 1);
    }

    vec3s positions = {0};
    vec3s normals = {0};

    char* line = strtok(data, "\n");
    while (line != NULL) {
        if (line[0] == 'v' && line[1] == ' ') {
            vec3 position;
            if (sscanf(line, "v %f %f %f", &position.x, &position.y, &position.z) == 3) {
                da_append(&positions, position);
            }
        } else if(line[0] == 'v' && line[1] == 'n'){
            vec3 normal;
            if (sscanf(line, "vn %f %f %f", &normal.x, &normal.y, &normal.z) == 3) {
                da_append(&normals, normal);
            }
        }else if (line[0] == 'f' && line[1] == ' ') {
            size_t a, b, c;
            size_t na, nb, nc;
            // Try full format first
            if (sscanf(line, "f %zu/%*zu/%zu %zu/%*zu/%zu %zu/%*zu/%zu", &a, &na, &b, &nb, &c, &nc) == 6 ||
                sscanf(line, "f %zu//%zu %zu//%zu %zu//%zu", &a, &na, &b, &nb, &c, &nc) == 6){
                da_append(verticesOut, ((Vertex){
                    .position = positions.items[a-1],
                    .normal = normals.items[na-1],
                }));
                da_append(verticesOut, ((Vertex){
                    .position = positions.items[c-1],
                    .normal = normals.items[nc-1],
                }));
                da_append(verticesOut, ((Vertex){
                    .position = positions.items[b-1],
                    .normal = normals.items[nb-1],
                }));
            }
        }

        line = strtok(NULL, "\n");
    }

    da_free(positions);
    da_free(normals);

    return;
}

size_t verticesCount;

int main(int argc, char** argv){
    char* program_name = shift_args(&argc,&argv);
    char* model_name = "assets/cube.obj";
    if(argc > 0){
        model_name = shift_args(&argc,&argv);
    }

    engineInit("Cube Example", 640, 480);
    afterResize();
    platform_set_mouse_position(swapchainExtent.width/2, swapchainExtent.height/2);

    String_Builder sb = {0};
    read_entire_file("assets/shaders/compiled/model.vert.spv", &sb);

    VkShaderModule vertexShader;
    if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count,&vertexShader)) return 1;

    sb.count = 0;
    read_entire_file("assets/shaders/compiled/phong.frag.spv", &sb);

    VkShaderModule fragmentShader;
    if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count,&fragmentShader)) return 1;

    VkVertexInputAttributeDescription vertexInputAttributeDescription[] = {
        (VkVertexInputAttributeDescription){
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position),
        },
        (VkVertexInputAttributeDescription){
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, normal),
        },
    };

    if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .pipelineOUT = &pipeline,
        .pipelineLayoutOUT = &pipelineLayout,
        .pushConstantsSize = sizeof(pcs),
        .vertexSize = sizeof(Vertex),
        .vertexInputAttributeDescriptionsCount = ARRAY_LEN(vertexInputAttributeDescription),
        .vertexInputAttributeDescriptions = vertexInputAttributeDescription,
        .depthTest = true,
        .depthWrite = true,
        .culling = true,
    })) return 1;

    sb.count = 0;
    read_entire_file(model_name,&sb);
    sb_append_null(&sb);

    Vertices vertices = {0};
    wavefrontParse(sb.items, &vertices);

    pcs.model = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,-5,1,
    };

    verticesCount = vertices.count;

    if(!createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vertices.items[0])*vertices.count, &verticesBuffer, &verticesMemory)) return 1;
    if(!transferDataToMemory(verticesMemory,vertices.items, 0, sizeof(vertices.items[0])*vertices.count)) return 1;

    return engineStart();
}

bool afterResize(){
    pcs.proj = perspective(90.0f,(float)swapchainExtent.width/(float)swapchainExtent.height, 0.001f, 100.0f);

    destroyDepthImage();
    initDepthImage();
    return true;
}

bool capture = false;

bool update(float deltaTime){
    vec3 forward = {sinf(rot.x),0,-cosf(rot.x)};
    vec3 right = {cosf(rot.x),0,sinf(rot.x)};


    if(input.keys[KEY_W].isDown){
        position.x += forward.x * deltaTime;
        position.z += forward.z * deltaTime;
    }

    if(input.keys[KEY_S].isDown){
        position.x -= forward.x * deltaTime;
        position.z -= forward.z * deltaTime;
    }

    if(input.keys[KEY_A].isDown){
        position.x -= right.x * deltaTime;
        position.z -= right.z * deltaTime;
    }

    if(input.keys[KEY_D].isDown){
        position.x += right.x * deltaTime;
        position.z += right.z * deltaTime;
    }

    if(input.keys[KEY_SPACE].isDown) position.y += deltaTime;
    if(input.keys[KEY_SHIFT].isDown) position.y -= deltaTime;

    if(input.keys[KEY_UP].isDown) rot.y += deltaTime;
    if(input.keys[KEY_DOWN].isDown) rot.y -= deltaTime;
    if(input.keys[KEY_LEFT].isDown) rot.x -= deltaTime;
    if(input.keys[KEY_RIGHT].isDown) rot.x += deltaTime;

    if(input.keys[KEY_F].justPressed) capture = !capture;

    if(capture){
        float sensitivity = 1000.0f;

        float valX =  (float)((int)input.mouse_x - (int)swapchainExtent.width/2)  / sensitivity;
        float valY =  (float)((int)input.mouse_y - (int)swapchainExtent.height/2) / sensitivity;
        rot.x += valX;
        rot.y -= valY;
        platform_set_mouse_position(swapchainExtent.width/2, swapchainExtent.height/2);
    }

    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -position.x,-position.y,-position.z,1,
    };

    //pitch
    mat4 rotation_matrix = (mat4){
        1,0,0,0,
        0,cosf(rot.y),sinf(rot.y),0,
        0,-sinf(rot.y),cosf(rot.y),0,
        0,0,0,1,
    };

    // yaw
    rotation_matrix = mat4mul(&(mat4){
        cosf(rot.x),0,sinf(rot.x),0,
        0,1,0,0,
        -sinf(rot.x),0,cosf(rot.x),0,
        0,0,0,1,
    }, &rotation_matrix);

    rotation_matrix = mat4transpose(&rotation_matrix);

    pcs.view = mat4mul(&rotation_matrix, &pcs.view);

    return true;
}

bool draw(){
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
        .depthAttachment = depthImageView,
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height,
        .minDepth = 0.0,
        .maxDepth = 1.0
    });
    
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent
    });

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(pcs), &pcs);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd,0,1,&verticesBuffer,&offset);

    vkCmdDraw(cmd,verticesCount,1,0,0);

    vkCmdEndRendering(cmd);

    return true;
}
