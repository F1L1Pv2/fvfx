#include "vulkanizer.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_buffer.h"
#include "engine/vulkan_images.h"
#include "shader_utils.h"

#define FA_REALLOC(optr, osize, new_size) realloc(optr, new_size)
#define fa_reserve(da, extra) \
   do {\
      if((da)->count + extra >= (da)->capacity) {\
          void* _da_old_ptr;\
          size_t _da_old_capacity = (da)->capacity;\
          (void)_da_old_capacity;\
          (void)_da_old_ptr;\
          (da)->capacity = (da)->capacity*2+extra;\
          _da_old_ptr = (da)->items;\
          (da)->items = FA_REALLOC(_da_old_ptr, _da_old_capacity*sizeof(*(da)->items), (da)->capacity*sizeof(*(da)->items));\
          assert((da)->items && "Ran out of memory");\
      }\
   } while(0)
#define fa_push(da, value) \
   do {\
        fa_reserve(da, 1);\
        (da)->items[(da)->count++]=value;\
   } while(0)

typedef struct{
    int currentImage;
    
    VkImage image1;
    VkDeviceMemory imageMemory1;
    VkImageView imageView1;
    size_t imageStride1;
    void* imageMapped1;
    VkDescriptorSet descriptorSet1;
    
    VkImage image2;
    VkDeviceMemory imageMemory2;
    VkImageView imageView2;
    size_t imageStride2;
    void* imageMapped2;
    VkDescriptorSet descriptorSet2;
} VulkanizerImagesOut;

typedef struct{
    VulkanizerImagesOut* items;
    size_t count;
    size_t capacity;
    size_t used;
    size_t item_size;
} VulkanizerImagesOutPool;

static bool applyShadersOnFrame(
                            VkCommandBuffer cmd,
                            size_t inWidth,
                            size_t inHeight,
                            size_t outWidth,
                            size_t outHeight,
                            void* push_constants_data,
                            size_t push_constants_size,
                            
                            VkDescriptorSet* inImageDescriptorSet,
                            VkImageView outImageView, 

                            VulkanizerVfx* vfx
                        ){
    if(push_constants_size != vfx->module.pushContantsSize){
        fprintf(stderr, "Expected push contants to have %zu bytes but got %zu bytes!\n", vfx->module.pushContantsSize, push_constants_size);
        return false;
    }

    vkCmdBeginRenderingEX(cmd,
        .colorAttachment = outImageView,
        .clearColor = COL_EMPTY,
        .renderArea = (
            (VkExtent2D){.width = outWidth, .height= outHeight}
        )
    );

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = outWidth,
        .height = outHeight
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = (VkExtent2D){.width = outWidth, .height = outHeight},
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, vfx->pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,vfx->pipelineLayout,0,1,inImageDescriptorSet,0,NULL);
    float constants[4] = {
        outWidth, outHeight,
        inWidth, inHeight
    };
    vkCmdPushConstants(cmd, vfx->pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(constants), constants);
    if(push_constants_data != NULL && push_constants_size > 0) vkCmdPushConstants(cmd, vfx->pipelineLayout, VK_SHADER_STAGE_ALL, sizeof(constants), push_constants_size, push_constants_data);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRendering(cmd);

    return true;
}

bool createMyImage(VkDevice device, VkImage* image, size_t width, size_t height, VkDeviceMemory* imageMemory, VkImageView* imageView, size_t* imageStride, void** imageMapped, VkImageUsageFlagBits imageUsage, VkMemoryPropertyFlagBits memoryProperty){
    if(!vkCreateImageEX(device, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
            imageUsage,
            memoryProperty, image,imageMemory)){
        printf("Couldn't create image\n");
        return false;
    }

    if(!vkCreateImageViewEX(device, *image,VK_FORMAT_R8G8B8A8_UNORM, 
                VK_IMAGE_ASPECT_COLOR_BIT, imageView)){
        printf("Couldn't create image view\n");
        return false;
    }

    if(imageStride) *imageStride = vkGetImageStride(device, *image);
    if(imageMemory && imageStride) vkMapMemory(device,*imageMemory, 0, (*imageStride)*height, 0, imageMapped);

    return true;
}

static bool init_output_image(
    VkDevice device,
    VkDescriptorPool descriptorPool,

    size_t outWidth,
    size_t outHeight,

    VkDescriptorSetLayout descriptorSetLayout,
    VkSampler samplerLinear,

    VkImage* outImage1,
    VkDeviceMemory* outImageMemory1,
    VkImageView* outImageView1,
    size_t* outImageStride1,
    void** outImageMapped1,
    VkDescriptorSet* outImageDescriptorSet1,

    VkImage* outImage2,
    VkDeviceMemory* outImageMemory2,
    VkImageView* outImageView2,
    size_t* outImageStride2,
    void** outImageMapped2,
    VkDescriptorSet* outImageDescriptorSet2
){
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

    vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, outImageDescriptorSet1);
    vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, outImageDescriptorSet2);

    if(!createMyImage(device, outImage1,
        outWidth,
        outHeight,
        outImageMemory1, outImageView1,
        outImageStride1, 
        outImageMapped1,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;

    if(!createMyImage(device, outImage2,
        outWidth,
        outHeight,
        outImageMemory2, outImageView2, 
        outImageStride2, 
        outImageMapped2,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;

    VkCommandBuffer tempCmd = vkCmdBeginSingleTime();
    vkCmdTransitionImage(tempCmd,*outImage1, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdTransitionImage(tempCmd,*outImage2, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkCmdEndSingleTime(tempCmd);

    {
        VkDescriptorImageInfo descriptorImageInfo = {0};
        VkWriteDescriptorSet writeDescriptorSet = {0};

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = *outImageView1;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = *outImageDescriptorSet1;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        descriptorImageInfo.imageView = *outImageView2;
        writeDescriptorSet.dstSet = *outImageDescriptorSet2;
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
    }
    return true;
}

static VulkanizerImagesOut* VulkanizerImagesOutPool_get_avaliable(Vulkanizer* vulkanizer, VulkanizerImagesOutPool* pool){
    if(pool->used < pool->count) return &pool->items[pool->used++];
    fa_reserve(pool, pool->count + 1);
    VulkanizerImagesOut* out = &pool->items[pool->count++];

    //initalization
    *out = (VulkanizerImagesOut){0};
    if(!init_output_image(
        vulkanizer->device,
        vulkanizer->descriptorPool,
        vulkanizer->videoOutWidth, vulkanizer->videoOutHeight,
        vulkanizer->vfxDescriptorSetLayout,
        vulkanizer->samplerLinear,

        &out->image1,
        &out->imageMemory1,
        &out->imageView1,
        &out->imageStride1,
        &out->imageMapped1,
        &out->descriptorSet1,

        &out->image2,
        &out->imageMemory2,
        &out->imageView2,
        &out->imageStride2,
        &out->imageMapped2,
        &out->descriptorSet2
    )) return NULL;

    return out;
}

static void VulkanizerImagesOutPool_reset(VulkanizerImagesOutPool* pool){
    pool->used = 0;
}

static VulkanizerImagesOutPool vulkanizerImagesOutPool = {0};

bool Vulkanizer_init(VkDevice deviceIN, VkDescriptorPool descriptorPoolIN, size_t outWidth, size_t outHeight, Vulkanizer* vulkanizer, ArenaAllocator* aa){
    vulkanizer->aa = aa;
    vulkanizer->device = deviceIN;
    vulkanizer->descriptorPool = descriptorPoolIN;

    if(vkCreateSampler(vulkanizer->device, &(VkSamplerCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .mipLodBias = 0.0f,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
    }, NULL, &vulkanizer->samplerLinear) != VK_SUCCESS) return false;

    const char* vertexShaderSrc = 
        "#version 450\n"
        "layout(location = 0) out vec2 uv;\n"
        "void main() {"
            "uint b = 1 << (gl_VertexIndex % 6);"
            "vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);"
            "uv = baseCoord;"
            "gl_Position = vec4(baseCoord * 2 - 1, 0.0f, 1.0f);"
        "}";


    if(!vkCompileShader(vulkanizer->device,vertexShaderSrc, shaderc_vertex_shader, &vulkanizer->vertexShader)) return false;

    const char* fragmentShaderSrc =
        "#version 450\n"
        "layout(location = 0) out vec4 outColor;\n"
        "layout(location = 0) in vec2 uv;\n"
        "layout(set = 0, binding = 0) uniform sampler2D imageIN;\n"
        "void main() {\n"
            "outColor = texture(imageIN, uv);\n"
        "}\n";

    VkShaderModule fragmentShader;
    if(!vkCompileShader(vulkanizer->device,fragmentShaderSrc, shaderc_fragment_shader, &fragmentShader)) return false;

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
    descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBinding.descriptorCount = 1;
    descriptorSetLayoutBinding.binding = 0;
    descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount  = 1;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

    if(vkCreateDescriptorSetLayout(vulkanizer->device, &descriptorSetLayoutCreateInfo, NULL, &vulkanizer->vfxDescriptorSetLayout) != VK_SUCCESS){
        printf("ERROR\n");
        return false;
    }

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    if(!vkCreateGraphicPipeline(
        vulkanizer->vertexShader,fragmentShader, 
        &vulkanizer->defaultPipeline, 
        &vulkanizer->defaultPipelineLayout,
        colorFormat,
        .descriptorSetLayoutCount = 1,
        .descriptorSetLayouts = &vulkanizer->vfxDescriptorSetLayout,
    )) return false;

    vulkanizer->videoOutWidth = outWidth;
    vulkanizer->videoOutHeight = outHeight;
    
    return true;
}

bool Vulkanizer_init_image_for_media(Vulkanizer* vulkanizer, size_t width, size_t height, VkImage* imageOut, VkDeviceMemory* imageMemoryOut, VkImageView* imageViewOut, size_t* imageStrideOut, VkDescriptorSet* descriptorSetOut, void* imageDataOut){
    if(!createMyImage(vulkanizer->device, imageOut,
        width, 
        height, 
        imageMemoryOut, imageViewOut, 
        imageStrideOut, 
        imageDataOut,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;

    VkCommandBuffer tempCmd = vkCmdBeginSingleTime();
    vkCmdTransitionImage(tempCmd, *imageOut, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkCmdEndSingleTime(tempCmd);
    
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = vulkanizer->descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &vulkanizer->vfxDescriptorSetLayout;
    if(vkAllocateDescriptorSets(vulkanizer->device, &descriptorSetAllocateInfo, descriptorSetOut) != VK_SUCCESS) return false;

    {
        VkDescriptorImageInfo descriptorImageInfo = {0};
        VkWriteDescriptorSet writeDescriptorSet = {0};

        descriptorImageInfo.sampler = vulkanizer->samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = *imageViewOut;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = *descriptorSetOut;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(vulkanizer->device, 1, &writeDescriptorSet, 0, NULL);
    }
    return true;
}

void Vulkanizer_reset_pool(){
    VulkanizerImagesOutPool_reset(&vulkanizerImagesOutPool);
}

bool Vulkanizer_apply_vfx_on_frame_and_compose(VkCommandBuffer cmd, Vulkanizer* vulkanizer, VulkanizerVfxInstances* vfxInstances, VkImageView videoInView, void* videoInData, size_t videoInStride, VkDescriptorSet videoInDescriptorSet, Frame* frameIn, VkImageView composedOutView){
    if(frameIn->type != FRAME_TYPE_VIDEO) return false;

    VulkanizerImagesOut* usedImages = VulkanizerImagesOutPool_get_avaliable(vulkanizer, &vulkanizerImagesOutPool);
    if(usedImages == NULL) return false;

    for(int i = 0; i < frameIn->video.height; i++){
        memcpy(
            (uint8_t*)videoInData + videoInStride*i,
            (uint8_t*)frameIn->video.data + frameIn->video.width*sizeof(uint32_t)*i,
            frameIn->video.width *sizeof(uint32_t)
        );
    }

    vkCmdTransitionImage(
        cmd, usedImages->currentImage == 0 ? usedImages->image1 : usedImages->image2, 
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_PIPELINE_STAGE_TRANSFER_BIT, 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    {
        vkCmdBeginRenderingEX(cmd,
            .colorAttachment = usedImages->currentImage == 0 ? usedImages->imageView1 : usedImages->imageView2,
            .clearColor = COL_EMPTY,
            .renderArea = (
                (VkExtent2D){.width = vulkanizer->videoOutWidth, .height= vulkanizer->videoOutHeight}
            )
        );

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = vulkanizer->videoOutWidth,
            .height = vulkanizer->videoOutHeight
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent = (VkExtent2D){.width = vulkanizer->videoOutWidth, .height = vulkanizer->videoOutHeight},
        });

        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanizer->defaultPipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,vulkanizer->defaultPipelineLayout,0,1,&videoInDescriptorSet,0,NULL);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    for(size_t i = 0; i < vfxInstances->count; i++){
        VulkanizerVfxInstance* vfx = &vfxInstances->items[i];
        if(vfx->push_constants_data != NULL && vfx->push_constants_size != vfx->vfx->module.pushContantsSize){
            fprintf(stderr, "%zu %s Invalid push contants size expected %zu got %zu\n", i, vfx->vfx->module.name, vfx->vfx->module.pushContantsSize, vfx->push_constants_size);
            return false;
        }


        vkCmdTransitionImage(
            cmd, usedImages->currentImage == 0 ? usedImages->image1 : usedImages->image2, 
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );

        vkCmdTransitionImage(
            cmd, usedImages->currentImage == 1 ? usedImages->image1 : usedImages->image2, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        );

        if(!applyShadersOnFrame(
                cmd,
                frameIn->video.width,
                frameIn->video.height,
                vulkanizer->videoOutWidth,
                vulkanizer->videoOutHeight,
                vfx->push_constants_data,
                vfx->push_constants_size,

                usedImages->currentImage == 0 ? &usedImages->descriptorSet1 : &usedImages->descriptorSet2,
                usedImages->currentImage == 1 ? usedImages->imageView1 : usedImages->imageView2,
                vfx->vfx
            )) return false;
        
        usedImages->currentImage = 1 - usedImages->currentImage;
    }

    //compositing
    vkCmdTransitionImage(
        cmd, usedImages->currentImage == 0 ? usedImages->image1 : usedImages->image2, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );

    {
        vkCmdBeginRenderingEX(cmd,
            .colorAttachment = composedOutView,
            .clearBackground = false,
            .renderArea = (
                (VkExtent2D){.width = vulkanizer->videoOutWidth, .height= vulkanizer->videoOutHeight}
            )
        );

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = vulkanizer->videoOutWidth,
            .height = vulkanizer->videoOutHeight
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent = (VkExtent2D){.width = vulkanizer->videoOutWidth, .height = vulkanizer->videoOutHeight},
        });

        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanizer->defaultPipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,vulkanizer->defaultPipelineLayout,0,1,usedImages->currentImage == 0 ? &usedImages->descriptorSet1 : &usedImages->descriptorSet2,0,NULL);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    vkCmdTransitionImage(
        cmd, usedImages->currentImage == 0 ? usedImages->image1 : usedImages->image2, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    return true;
}

static String_Builder sb = {0};
bool Vulkanizer_init_vfx(Vulkanizer* vulkanizer, const char* filename, VulkanizerVfx* outVfx){
    sb.count = 0;
    if(!read_entire_file(filename,&sb)) return false;
    if(!extractVFXModuleMetaData(nob_sb_to_sv(sb),&outVfx->module, vulkanizer->aa)) return false;
    if(!preprocessVFXModule(&sb, &outVfx->module)) return false;
    sb_append_null(&sb);

    VkShaderModule fragmentShader;
    if(!vkCompileShader(vulkanizer->device,sb.items,shaderc_fragment_shader,&fragmentShader)) return false;

    for(size_t i = 0; i < outVfx->module.inputs.count; i++){
        outVfx->module.pushContantsSize += get_vfxInputTypeSize(outVfx->module.inputs.items[i].type);
    }

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if(!vkCreateGraphicPipeline(
        vulkanizer->vertexShader,fragmentShader, 
        &outVfx->pipeline, 
        &outVfx->pipelineLayout,
        colorFormat,
        .pushConstantsSize = sizeof(float)*4 + outVfx->module.pushContantsSize,
        .descriptorSetLayoutCount = 1,
        .descriptorSetLayouts = &vulkanizer->vfxDescriptorSetLayout,
    )) return false;

    vkDestroyShaderModule(vulkanizer->device, fragmentShader, NULL);

    outVfx->module.filepath = filename;
    return true;
}