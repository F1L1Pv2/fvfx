#include <stdio.h>
#include <stdbool.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"

#include "engine/engine.h"
#include "engine/app.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_buffer.h"
#include "engine/vulkan_images.h"

#include "math.h"
#include "modules/gmath.h"
#include "modules/bindlessTexturesManager.h"
#include "modules/spriteManager.h"

typedef struct{
    mat4 proj;
    mat4 view;
    VkDeviceAddress SpriteDrawBufferPtr;
} PushConstants;

static PushConstants pcs;

mat4 ortho2D(float width, float height){
    float left = -width/2;
    float right = width/2;
    float top = height/2;
    float bottom = -height/2;

    return (mat4){
    2 / (right - left),0                 , 0, -(right + left) / (right - left),
          0           ,2 / (top - bottom), 0, -(top + bottom) / (top - bottom),
          0           ,     0            ,-1,                 0,
          0           ,     0            , 0,                 1,
    };
}

bool afterResize(){
    pcs.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };

    return true;
}

static VkPipeline pipeline;
static VkPipelineLayout pipelineLayout;

size_t lenaTextureID;

int main(){
    if(!engineInit("FVFX", 640,480)) return 1;

    {
        File_Paths initialTextures = {0};
        da_append(&initialTextures,"assets/test.png");
        if(!initBindlessTextures(initialTextures)) return 1;
        lenaTextureID = getTextureID("assets/test.png");

        String_Builder sb = {0};
        nob_read_entire_file("assets/shaders/compiled/sprite.vert.spv",&sb);
        sb_append_null(&sb);
        
        VkShaderModule vertexShader;
        // if(!compileShader(sb.items, shaderc_vertex_shader,&vertexShader)) return false;
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/sprite.frag.spv",&sb);
        sb_append_null(&sb);
        
        VkShaderModule fragmentShader;
        // if(!compileShader(sb.items, shaderc_fragment_shader,&fragmentShader)) return false;
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pushConstantsSize = sizeof(PushConstants),
            .pipelineOUT = &pipeline, 
            .pipelineLayoutOUT = &pipelineLayout,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &bindlessDescriptorSetLayout,
        })) return false;
        
        afterResize();
        if(!initSpriteManager()) return 1;
        pcs.SpriteDrawBufferPtr = vkGetBufferDeviceAddressEX(spriteDrawBuffer);

        da_free(sb);
    }

    return engineStart();
}

float time;

vec2 pos = (vec2){300, 50};
vec2 acc = (vec2){5.0f,-2.0};
vec2 size = (vec2){200,200};

size_t jimboTextureID = -1;

bool update(float deltaTime){
    time += deltaTime;

    acc.y += deltaTime * 9.8f;
    
    if(pos.x + size.x + acc.x < swapchainExtent.width && pos.x + acc.x > 0){
        pos.x += acc.x;
    }else{
        acc.x = acc.x * -1.0f;
    }

    if(pos.y + size.y + acc.y < swapchainExtent.height){
        pos.y += acc.y;
    }else{
        acc.y = acc.y * -0.96f;
    }

    drawSprite((SpriteDrawCommand){
        .transform = (mat4){
            200,0,0,0,
            0,200,0,0,
            0,0,1,0,
            0,0,0,1,
        },
        .textureID = -1,
        .albedo = (vec3){0.0,1.0,0.0},
    });

    size_t textureID;

    if(jimboTextureID == -1){
        textureID = lenaTextureID;
    }else{
        textureID = (uint32_t)(floorf(time*10)) % 2 ? lenaTextureID : jimboTextureID;
    }

    if(time > 5.0f && jimboTextureID == -1){
        jimboTextureID = addBindlessTextureFromDisk("assets/Jimbo100x.png");
    }

    drawSprite((SpriteDrawCommand){
        .transform = (mat4){
            size.x,0,0,0,
            0,size.y,0,0,
            0,0,1,0,
            pos.x,pos.y,0,1,
        },
        .textureID = textureID,
        .albedo = (vec3){1.0,0.0,0.0},
    });

    return true;
}

bool draw(){
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
        .clearColor = (Color){18.0f/255.f,18.0f/255.f,18.0f/255.f,1.0f}
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent,
    });
        
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&bindlessDescriptorSet,0,NULL);

    vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(PushConstants), &pcs);

    renderSprites();

    vkCmdEndRendering(cmd);

    return true;
}