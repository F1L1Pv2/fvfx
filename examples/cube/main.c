#include "engine/engine.h"
#include "engine/app.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"
#include "engine/vulkan_buffer.h"
#include "engine/input.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

VkPipeline pipeline;
VkPipelineLayout pipelineLayout;

#include "3rdparty/stb_image.h"

#include "modules/gmath.h"

#include "math.h"

typedef struct{
    vec3 position;
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

size_t indicesCount;


vec2 rot = {0};
vec3 position = (vec3){0,0,-5};

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
    return true;
}

int main(){
    engineInit("normal_texture Example", 640, 480);
    afterResize();

    String_Builder sb = {0};
    read_entire_file("assets/shaders/compiled/model.vert.spv", &sb);

    VkShaderModule vertexShader;
    if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count,&vertexShader)) return 1;

    sb.count = 0;
    read_entire_file("assets/shaders/compiled/phong.frag.spv", &sb);

    VkShaderModule fragmentShader;
    if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count,&fragmentShader)) return 1;

    if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .pipelineOUT = &pipeline,
        .pipelineLayoutOUT = &pipelineLayout,
        .pushConstantsSize = sizeof(pcs),
        .vertexSize = sizeof(Vertex),
        .vertexInputAttributeDescriptionsCount = 1,
        .vertexInputAttributeDescriptions = &(VkVertexInputAttributeDescription){
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position),
        },
        .depthTest = true,
        .depthWrite = true,
        .culling = true,
    })) return 1;

    Vertex vertices[] = {
        {-1,-1,-1}, { 1,-1,-1}, {-1,-1, 1}, { 1,-1, 1},

        {-1, 1,-1}, { 1, 1,-1}, {-1, 1, 1}, { 1, 1, 1},
    };

    uint32_t indices[] = {
        2,7,3, 2,6,7,

        1,4,0, 1,5,4,

        0,3,1, 0,2,3,

        4,5,7, 4,7,6,

        0,6,2, 0,4,6,

        1,7,5, 1,3,7,
    };

    pcs.model = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1,
    };

    indicesCount = ARRAY_LEN(indices);

    if(!createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,sizeof(indices), &indicesBuffer,&indicesMemory)) return 1;
    if(!transferDataToMemory(indicesMemory,indices,0,sizeof(indices))) return 1;

    if(!createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vertices), &verticesBuffer, &verticesMemory)) return 1;
    if(!transferDataToMemory(verticesMemory,vertices, 0, sizeof(vertices))) return 1;

    return engineStart();
}

bool afterResize(){
    pcs.proj = perspective(90.0f,(float)swapchainExtent.width/(float)swapchainExtent.height, 0.001f, 100.0f);

    destroyDepthImage();
    initDepthImage();
    return true;
}

bool update(float deltaTime){
    vec3 forward = {sinf(rot.x),0,cosf(rot.x)};
    vec3 right = {-cosf(rot.x),0,sinf(rot.x)};


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

    if(input.keys[KEY_SPACE].isDown) position.y -= deltaTime;
    if(input.keys[KEY_SHIFT].isDown) position.y += deltaTime;

    if(input.keys[KEY_UP].isDown) rot.y += deltaTime;
    if(input.keys[KEY_DOWN].isDown) rot.y -= deltaTime;
    if(input.keys[KEY_LEFT].isDown) rot.x += deltaTime;
    if(input.keys[KEY_RIGHT].isDown) rot.x -= deltaTime;

    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        position.x,position.y,position.z,1,
    };

    //yaw
    pcs.view = mat4mul((mat4){
        cosf(rot.x),0,sinf(rot.x),0,
        0,1,0,0,
        -sinf(rot.x),0,cosf(rot.x),0,
        0,0,0,1,
    }, pcs.view);

    //pitch
    pcs.view = mat4mul(pcs.view, (mat4){
        1,0,0,0,
        0,cosf(rot.y),sinf(rot.y),0,
        0,-sinf(rot.y),cosf(rot.y),0,
        0,0,0,1,
    });

    return true;
}

bool draw(){
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
        .depthAttachment = depthImageView,
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
    
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent
    });

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(pcs), &pcs);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd,0,1,&verticesBuffer,&offset);
    vkCmdBindIndexBuffer(cmd,indicesBuffer,0,VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd,indicesCount,1,0,0,0);

    vkCmdEndRendering(cmd);

    return true;
}