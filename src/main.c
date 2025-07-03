#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

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
#include "engine/input.h"

#include "math.h"
#include "modules/gmath.h"
#include "modules/bindlessTexturesManager.h"
#include "modules/spriteManager.h"
#include "modules/font_freetype.h"
#include "ffmpeg_video.h"
#include "ffmpeg_audio.h"
#include "gui_helpers.h"
#include "sound_engine.h"
#include <stdatomic.h>
#include "string_alloc.h"

StringAllocator sa = {0};

typedef struct{
    mat4 projView;
    VkDeviceAddress SpriteDrawBufferPtr;
} PushConstants;

typedef struct{
    mat4 projView;
    mat4 model;
} PushConstantsPreview;

static PushConstants pcs;
static PushConstantsPreview pcsPreview;

bool afterResize(){
    mat4 ortho = ortho2D(swapchainExtent.width, swapchainExtent.height);
    mat4 projView = mat4mul(&ortho, &(mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    });

    pcs.projView = projView;
    pcsPreview.projView = projView;

    return true;
}

static VkPipeline pipeline;
static VkPipelineLayout pipelineLayout;

static VkPipeline pipelineImagePreview;
static VkPipelineLayout pipelineImageLayoutPreview;
static VkDescriptorSet previewDescriptorSet;

static VkPipeline pipelinePreview;
static VkPipelineLayout pipelineLayoutPreview;
static VkDescriptorSet outDescriptorSet1;
static VkDescriptorSet outDescriptorSet2;
static VkDescriptorSetLayout vfxDescriptorSetLayout;

VkImage previewImage1;
VkDeviceMemory previewMemory1;
VkImageView previewView1;

VkImage previewImage2;
VkDeviceMemory previewMemory2;
VkImageView previewView2;

GlyphAtlas atlas = {0};

#include "engine/platform.h"

uint64_t TIMER;
uint64_t TIMER_TOTAL;
#define CHECK_TIMER(thing) do {uint64_t newTimer = platform_get_time();printf("%s: took %.2fs\n", (thing), (float)(newTimer - TIMER) / 1000.0f);TIMER = newTimer;} while(0)
#define CHECK_TIMER_TOTAL(thing) do {uint64_t newTimer = platform_get_time();printf("%s: took %.2fs\n", (thing), (float)(newTimer - TIMER_TOTAL) / 1000.0f);TIMER_TOTAL = newTimer;} while(0)

Video video = {0};
Frame videoFrame = {0};
void* videoMapped = NULL;
size_t videoVulkanStride = 0;
Audio audio = {0};
bool audioInMedia = false;

typedef enum {
    VFX_NONE = 0,
    VFX_FLOAT,
} VfxInputType;

typedef struct{
    VfxInputType type;
    const char* name;
} VfxInput;

typedef struct{
    VfxInput* items;
    size_t count;
    size_t capacity;
} VfxInputs;

typedef struct {
    const char* filepath;
    const char* name;
    const char* description;
    const char* author;
    VfxInputs inputs;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    size_t pushContantsSize;
} VfxModule;

typedef struct{
    VfxModule* module;
    void *inputPushConstants;
    bool opened;
} VfxInstance;

typedef struct{
    VfxInstance* items;
    size_t count;
    size_t capacity;
} VfxInstances;

typedef struct HashItem HashItem;

struct HashItem {
    HashItem* next;
    String_View key;
    VfxModule value;
};

typedef struct {
    HashItem** buckets;
    size_t bucket_count;
} Hashmap;

void initHashMap(Hashmap* hashmap, size_t size) {
    hashmap->buckets = calloc(size, sizeof(HashItem*));
    hashmap->bucket_count = size;
}

uint32_t hashFunction(const char *key, uint32_t map_size) {
    uint32_t hash = 5381;
    int c;
    
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    return hash % map_size;
}

HashItem* getFromHashMap(Hashmap* hashmap, const char* key) {
    uint32_t hash = hashFunction(key, hashmap->bucket_count);
    String_View sv = sv_from_cstr(key);
    HashItem** item_ptr = &hashmap->buckets[hash];

    while (*item_ptr) {
        if (sv_eq((*item_ptr)->key, sv)) {
            return *item_ptr;
        }
        item_ptr = &(*item_ptr)->next;
    }

    HashItem* new_item = calloc(1, sizeof(HashItem));
    new_item->key = sv_from_cstr(sa_strdup(&sa, key));
    new_item->next = NULL;

    *item_ptr = new_item;
    return new_item;
}

bool extractVFXModuleMetaData(String_View sv, VfxModule* out){
    sv = sv_trim_left(sv);
    if(sv.count < 2) {
        printf("Empty file!");
        return false;
    }
    if(sv.data[0] != '/' && sv.data[1] != '*') {
        printf("No metadata descriptor\n");
        return false;
    }
    sv.data += 2;
    sv = sv_trim_left(sv);
    if(sv.count == 0) {
        printf("Empty file!");
        return false;
    }

    String_Builder sb = {0};

    while(sv.data[0] != '*' && sv.data[1] != '/' && sv.count > 0){
        String_View leftSide = sv_chop_by_delim(&sv, ':');
        sv = sv_trim_left(sv);

        String_View arg = sv_chop_by_delim(&sv, '\n');
        if(arg.count == 0) printf("Expected value for "SV_Fmt"\n",SV_Arg(leftSide));
        if(sv_eq(leftSide, sv_from_cstr("Name"))){
            sb.count = 0;
            sb_append_buf(&sb, arg.data, arg.count);
            sb_append_null(&sb);
            out->name = sa_strdup(&sa, sb.items);
        }
        else if(sv_eq(leftSide, sv_from_cstr("Description"))){
            sb.count = 0;
            sb_append_buf(&sb, arg.data, arg.count);
            sb_append_null(&sb);
            out->description = sa_strdup(&sa, sb.items);
        }
        else if(sv_eq(leftSide, sv_from_cstr("Author"))){
            sb.count = 0;
            sb_append_buf(&sb, arg.data, arg.count);
            sb_append_null(&sb);
            out->author = sa_strdup(&sa, sb.items);
        }
        else if(sv_eq(leftSide, sv_from_cstr("Input"))){
            String_View inputName = sv_trim_left(arg);
            String_View inputType = sv_trim(sv_chop_by_delim(&inputName, ' '));

            if(inputType.count == 0){
                printf("Please provide type for MetaInput\n");
                return false;
            }

            if(inputName.count == 0){
                printf("Please provide name for MetaInput\n");
                return false;
            }

            VfxInput input = {0};
            
            if(sv_eq(inputType, sv_from_cstr("float"))) input.type = VFX_FLOAT;

            if(input.type == VFX_NONE){
                printf("Unknown input type: "SV_Fmt"\n", SV_Arg(inputType));
                return false;
            }
            
            sb.count = 0;
            sb_append_buf(&sb, inputName.data, inputName.count);
            sb_append_null(&sb);
            input.name = sa_strdup(&sa, sb.items);

            da_append(&out->inputs, input);
        }
        else{
            printf("Unknown metadata attribute: "SV_Fmt"\n", SV_Arg(leftSide));

            da_free(sb);
            return false;
        }
        sv = sv_trim_left(sv);
        if(sv.count == 0) {
            printf("No metadata ending '*/' reached end of file");
            da_free(sb);
            return false;
        }
    }

    da_free(sb);
    return true;
}

char* get_vfxInputTypeName(VfxInputType type){
    switch (type)
    {
        case VFX_FLOAT: return "float";
        default: UNREACHABLE("update this!");
    }
}

size_t get_vfxInputTypeSize(VfxInputType type){
    switch (type)
    {
        case VFX_FLOAT: return sizeof(float);
        default: UNREACHABLE("update this!");
    }
}

bool preprocessVFXModule(String_Builder* sb, VfxModule* module){
    String_Builder newSB = {0};

    const char* prepend = 
                        "#version 450\n"
                        "layout(location = 0) out vec4 outColor;\n"
                        "layout(location = 0) in vec2 uv;\n"
                        "layout (set = 0, binding = 0) uniform sampler2D imageIN;\n";

    sb_append_cstr(&newSB, prepend);

    /*
    
    layout (push_constant) uniform constants
    {
        mat4 projView;
        SpriteDrawBuffer spriteDrawBuffer;
    } Input;
    
    */


    if(module->inputs.count > 0){
        
        sb_append_cstr(&newSB, "layout (push_constant) uniform constants\n{\n");

        for(size_t i = 0; i < module->inputs.count; i++){
            assert(module->inputs.items[i].type != VFX_NONE);
            sb_append_cstr(&newSB, get_vfxInputTypeName(module->inputs.items[i].type));
            sb_append_cstr(&newSB, " ");
            sb_append_cstr(&newSB, module->inputs.items[i].name);
            sb_append_cstr(&newSB, ";\n");
        }

        sb_append_cstr(&newSB, "} Input;\n");
    }


    sb_append_buf(&newSB,sb->items,sb->count);

    da_free((*sb));

    *sb = newSB;

    return true;
}

Hashmap vfxModulesHashMap = {0};
VfxInstances currentModuleInstances = {0};

VkShaderModule vfxVertexShader;

int main(int argc, char** argv){
    if(argc < 2){
        printf("you need to provide filename\n");
        return 1;
    }
    TIMER = platform_get_time();
    TIMER_TOTAL = TIMER;
    if(!engineInit("FVFX", 640,480)) return 1;
    CHECK_TIMER("init engine");

    initHashMap(&vfxModulesHashMap, 100);

    String_Builder sb = {0};
    {
        afterResize();

        // ------------------ sprite manager initialization ------------------

        File_Paths initialTextures = {0};
        da_append(&initialTextures,"assets/DropdownDelete.png");
        da_append(&initialTextures,"assets/DropdownOpen.png");
        da_append(&initialTextures,"assets/DropdownClosed.png");
        if(!initBindlessTextures(initialTextures)) return 1;

        nob_read_entire_file("assets/shaders/compiled/sprite.vert.spv",&sb);
        sb_append_null(&sb);
        
        VkShaderModule vertexShader;
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/sprite.frag.spv",&sb);
        sb_append_null(&sb);
        
        VkShaderModule fragmentShader;
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pushConstantsSize = sizeof(PushConstants),
            .pipelineOUT = &pipeline, 
            .pipelineLayoutOUT = &pipelineLayout,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &bindlessDescriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;
        
        if(!initSpriteManager()) return 1;
        pcs.SpriteDrawBufferPtr = vkGetBufferDeviceAddressEX(spriteDrawBuffer);

        CHECK_TIMER("init sprite");

        // ------------------          INIT SDF            ------------------

        VkImage fontImage = NULL;
        VkDeviceMemory fontMemory = NULL;
        VkImageView fontImageView = NULL;

        if(!GetFontSDFAtlas("assets/font/VictorMono-Regular.ttf",&fontImage, &fontMemory, &fontImageView, &atlas)) return false;

        addBindlessTextureRaw((Texture){
            .name = "font",
            .width = atlas.width,
            .height = atlas.height,
            .image = fontImage,
            .memory = fontMemory,
            .imageView = fontImageView,
        });

        CHECK_TIMER("init sdf");

        // ------------------ video preview initialization ------------------

        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/normal_texture.vert.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/normal_texture.frag.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;

        VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
        descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorSetLayoutBinding.descriptorCount = 1;
        descriptorSetLayoutBinding.binding = 0;
        descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.bindingCount  = 1;
        descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

        if(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &vfxDescriptorSetLayout) != VK_SUCCESS){
            printf("ERROR\n");
            return 1;
        }

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &vfxDescriptorSetLayout;

        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &outDescriptorSet1);
        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &outDescriptorSet2);
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pushConstantsSize = sizeof(PushConstantsPreview),
            .pipelineOUT = &pipelinePreview, 
            .pipelineLayoutOUT = &pipelineLayoutPreview,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &vfxDescriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;

        VkImage image;
        VkImageView imageView;
        VkDeviceMemory imageMemory;

        if(!ffmpegVideoInit(argv[1], &video)) {
            printf("Couldn't load video\n");
            return 1;
        }

        if(!ffmpegVideoGetFrame(&video,&videoFrame)){
            printf("Couldn't get video frame\n");
            return 1;
        }

        if(!createImage(videoFrame.width,videoFrame.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &image,&imageMemory)){
            printf("Couldn't create video image\n");
            return 1;
        }

        if(!sendDataToImage(image,videoFrame.data,videoFrame.width, videoFrame.width*sizeof(uint32_t), videoFrame.height)){
            printf("Couldn't send frame data to video image\n");
            return 1;
        }

        if(!createImageView(image,VK_FORMAT_R8G8B8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, &imageView)){
            printf("Couldn't create video image view\n");
            return 1;
        }

        VkImageSubresource subresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .arrayLayer = 0
        };

        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(device, image, &subresource, &layout);
        videoVulkanStride = layout.rowPitch;

        vkMapMemory(device,imageMemory, 0, videoVulkanStride*videoFrame.height, 0, &videoMapped);

        CHECK_TIMER("init video");

        //initializing preview image (vfx pipeline stuff)
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/vfx_texture.vert.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;

        vfxVertexShader = vertexShader;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/vfx_texture.frag.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;

        // VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &vfxDescriptorSetLayout;

        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &previewDescriptorSet);
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pipelineOUT = &pipelineImagePreview, 
            .pipelineLayoutOUT = &pipelineImageLayoutPreview,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &vfxDescriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;

        // ----- binding ffmpeg video into preview image

        VkDescriptorImageInfo descriptorImageInfo = {0};
        VkWriteDescriptorSet writeDescriptorSet = {0};

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = imageView;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = previewDescriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        if(!createImage(videoFrame.width,videoFrame.height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   0, &previewImage1,&previewMemory1)){
            printf("Couldn't create preview image\n");
            return 1;
        }

        if(!createImageView(previewImage1,VK_FORMAT_B8G8R8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, &previewView1)){
            printf("Couldn't create preview image view\n");
            return 1;
        }

        if(!createImage(videoFrame.width,videoFrame.height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   0, &previewImage2,&previewMemory2)){
            printf("Couldn't create preview image\n");
            return 1;
        }

        if(!createImageView(previewImage2,VK_FORMAT_B8G8R8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, &previewView2)){
            printf("Couldn't create preview image view\n");
            return 1;
        }

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = previewView1;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = outDescriptorSet1;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = previewView2;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = outDescriptorSet2;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        CHECK_TIMER("init vfx pipeline");

        if(ffmpegAudioInit(argv[1], &audio)){
            audioInMedia = true;
        }

        CHECK_TIMER("init audio");
    }

    sb_free(sb);

    CHECK_TIMER_TOTAL("init");
    return engineStart();
}

float time;

atomic_bool playing = true;

String_Builder sb = {0};
VkShaderModule fragmentShader;

void drawCurrentModuleInstances(Rect vfxContainer,float deltaTime){
    const float ContainerHeight = UI_FONT_SIZE * 1.5;
    const float ContainerWidth = vfxContainer.width;

    float offset = 0;

    for(size_t i = 0; i < currentModuleInstances.count; i++){
        VfxInstance* instance = &currentModuleInstances.items[i];

        const float moduleX = vfxContainer.x;
        const float moduleY = vfxContainer.y + offset;

        Rect moduleRect = (Rect){
            .x = moduleX,
            .y = moduleY,
            .width = ContainerWidth,
            .height = ContainerHeight
        };

        bool hovering = pointInsideRect(input.mouse_x, input.mouse_y, moduleRect);

        if(input.scroll > 0 && i > 0 && hovering){
            VfxInstance copy = *instance;
            memcpy(&currentModuleInstances.items[i], &currentModuleInstances.items[i - 1], sizeof(VfxInstance));
            memcpy(&currentModuleInstances.items[i - 1], &copy, sizeof(VfxInstance));
        }

        if(input.scroll < 0 && i < currentModuleInstances.count-1 && hovering){
            VfxInstance copy = *instance;
            memcpy(&currentModuleInstances.items[i], &currentModuleInstances.items[i + 1], sizeof(VfxInstance));
            memcpy(&currentModuleInstances.items[i + 1], &copy, sizeof(VfxInstance));
        }

        drawSprite((SpriteDrawCommand){
            .position = (vec2){moduleX, moduleY},
            .scale = (vec2){ContainerWidth, ContainerHeight},
            .albedo = hovering ? hex2rgb(0xFF808080) : hex2rgb(0xFF404040)
        });

        drawText(instance->module->name, 0xFFFFFFFF, UI_FONT_SIZE, (Rect){
            .x = moduleX + ContainerWidth/2 - measureText(instance->module->name, UI_FONT_SIZE)/2,
            .y = moduleY
        });

        if(instance->module->inputs.count > 0){
            drawSprite((SpriteDrawCommand){
                .position = (vec2){moduleX + UI_FONT_SIZE/8, moduleY + ContainerHeight/2 - UI_FONT_SIZE/2},
                .scale = (vec2){UI_FONT_SIZE, UI_FONT_SIZE},
                .textureIDEffects = instance->opened ? getTextureID("assets/DropdownOpen.png") : getTextureID("assets/DropdownClosed.png")
            });
        }

        Rect deleteRect = (Rect){
            .x = moduleRect.x + moduleRect.width - UI_FONT_SIZE - UI_FONT_SIZE/8,
            .y = moduleRect.y,
            .width = UI_FONT_SIZE,
            .height = UI_FONT_SIZE
        };

        bool hoverDelete = pointInsideRect(input.mouse_x, input.mouse_y, deleteRect);

        drawSprite((SpriteDrawCommand){
            .position = (vec2){moduleX + ContainerWidth - UI_FONT_SIZE - UI_FONT_SIZE/8, moduleY + ContainerHeight/2 - UI_FONT_SIZE/2},
            .scale = (vec2){UI_FONT_SIZE, UI_FONT_SIZE},
            .textureIDEffects = getTextureID("assets/DropdownDelete.png") | (2 << 16),
            .albedo = hoverDelete ? hex2rgb(0xFFff3f3f) : (vec3){1,1,1}
        });

        if(instance->module->inputs.count > 0){
            if(input.keys[KEY_MOUSE_LEFT].justReleased && hovering && !hoverDelete) instance->opened = !instance->opened;
        }

        if(input.keys[KEY_MOUSE_LEFT].justReleased && hoverDelete) da_remove_at(&currentModuleInstances, i);

        offset += ContainerHeight;

        if(instance->opened && instance->module->inputs.count > 0){
            float openSize = UI_FONT_SIZE*1.5 * instance->module->inputs.count;

            Rect openRect = (Rect){
                .x = moduleRect.x,
                .y = moduleRect.y + moduleRect.height,
                .width = moduleRect.width,
                .height = openSize
            };

            drawSprite((SpriteDrawCommand){
                .position = (vec2){openRect.x, openRect.y},
                .scale = (vec2){openRect.width, openRect.height},
                .albedo = hex2rgb(0xFF909090)
            });

            drawSprite((SpriteDrawCommand){
                .position = (vec2){openRect.x, openRect.y + 1},
                .scale = (vec2){openRect.width, openRect.height - 1},
                .albedo = hex2rgb(0xFF404040)
            });

            size_t byteOffset = 0;

            for(size_t j = 0; j < instance->module->inputs.count; j++){
                VfxInput* input = &instance->module->inputs.items[j];
                drawText(input->name, 0xFFFFFFFF, UI_FONT_SIZE, (Rect){
                    .x = openRect.x,
                    .y = openRect.y + UI_FONT_SIZE*1.5*j,
                });

                switch (input->type)
                {
                case VFX_FLOAT:
                    {
                        const float floatInputWidth = openRect.width /3;

                        drawFloatBox_internal((Rect){
                            .x = openRect.x + openRect.width - floatInputWidth,
                            .y = openRect.y + UI_FONT_SIZE*1.5*j,
                            .width = floatInputWidth,
                            .height = UI_FONT_SIZE * 1.5,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset), 1000 * i + j);
                        break;
                    }
                
                default: UNREACHABLE("Implement This!");
                }

                byteOffset += get_vfxInputTypeSize(input->type);
            }

            offset += openSize;
        }

        offset += 2;
    }
}

float effectTabSplitterOffset = 150;
float timelineSplitterOffset = 100;

bool update(float deltaTime){
    temp_reset();
    if(audioInMedia){
        time = soundEngineGetTime();
    }else{
        if(playing) time += deltaTime;
    }

    if(input.keys[KEY_SPACE].justPressed) playing = !playing;

    Rect topBarRect = (Rect){
        .x = 0,
        .y = 0,
        .width = swapchainExtent.width,
        .height = 30,
    };

    Rect effectsTab = (Rect){
        .width = effectTabSplitterOffset,
        .height = swapchainExtent.height - topBarRect.height,
        .y = topBarRect.y+topBarRect.height,
        .x = swapchainExtent.width - effectTabSplitterOffset,
    };

    Rect timelineRect = (Rect){
        .height = timelineSplitterOffset,
    };

    Rect previewPos = (Rect){
        .width = swapchainExtent.width - effectsTab.width,
        .height = swapchainExtent.height - timelineRect.height - topBarRect.height,
        .y = topBarRect.y+topBarRect.height,
    };

    timelineRect.width = swapchainExtent.width - effectsTab.width;
    timelineRect.y = previewPos.y + previewPos.height;

    Rect previewRect = fitRectangle(previewPos, videoFrame.width, videoFrame.height);
    
    pcsPreview.model = (mat4){
        previewRect.width,0,0,0,
        0,previewRect.height,0,0,
        0,0,1,0,
        previewRect.x,previewRect.y,0,1,
    };

    if(platform_drag_and_drop_available() && pointInsideRect(input.mouse_x, input.mouse_y, effectsTab)){
        int count = -1;
        const char** dragndrop = platform_get_drag_and_drop_files(&count);

        printf("Got drag and drop mousex: %zu mousey: %zu\n", input.mouse_x, input.mouse_y);

        for(int i = 0; i < count; i++){

            HashItem* item = getFromHashMap(&vfxModulesHashMap, dragndrop[i]);
            if(item == NULL){
                printf("UNREACHABLE!\n");
                return false;
            }

            if(item->value.filepath == NULL){
                item->value.filepath = item->key.data;
    
                sb.count = 0;
                nob_read_entire_file(item->value.filepath,&sb);
                
                //REMOVING FOCKIN CARRIAGE RETURN!
                char* current_pos = sb.items;
                while ((current_pos = strchr(current_pos, '\r'))) {
                    memmove(current_pos, current_pos + 1, strlen(current_pos + 1) + 1);
                    sb.count--;
                }

                if(!extractVFXModuleMetaData(sb_to_sv(sb), &item->value)) return false;
                if(!preprocessVFXModule(&sb, &item->value)) return false;
                sb_append_null(&sb);
                
                if(!compileShader(sb.items,shaderc_fragment_shader,&fragmentShader)) return false;
                
                size_t pushContantsSize = 0;
                for(size_t i = 0; i < item->value.inputs.count; i++){
                    pushContantsSize += get_vfxInputTypeSize(item->value.inputs.items[i].type);
                }
                item->value.pushContantsSize = pushContantsSize;

                if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
                    .vertexShader = vfxVertexShader,
                    .fragmentShader = fragmentShader,
                    .pipelineOUT = &item->value.pipeline,
                    .pipelineLayoutOUT = &item->value.pipelineLayout,
                    .descriptorSetLayoutCount = 1,
                    .descriptorSetLayouts = &vfxDescriptorSetLayout,
                    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    .pushConstantsSize = pushContantsSize,
                })) return false;

                printf("COMPILIN!\n");
            }

            VfxInstance instance = {0};
            instance.module = &item->value;

            if(instance.module->pushContantsSize > 0){
                instance.inputPushConstants = calloc(instance.module->pushContantsSize,1);
            }
            
            da_append(&currentModuleInstances, instance);
        }
        
        platform_release_drag_and_drop(dragndrop, count);
    }

    if(playing){
        bool didSmth = false;
        while(time > videoFrame.frameTime){
            if(ffmpegVideoGetFrame(&video,&videoFrame)){
                didSmth = true;
            }else{
                break;
            }
        }

        if(didSmth){
            for(int i = 0; i < videoFrame.height; i++){
                memcpy(
                    videoMapped + videoVulkanStride*i,
                    videoFrame.data + videoFrame.width*sizeof(uint32_t)*i,
                    videoFrame.width *sizeof(uint32_t)
                );
            }
        }
    }

    if(time >= video.duration){
        time = 0;
        if(!ffmpegVideoSeek(&video, &videoFrame,time)) return false;
        if(audioInMedia) {
            ffmpegAudioSeek(&audio, time);
            soundEngineSetTime(time);
        }
    }

    // --------------------------- EFFECTS RACK --------------------------------
    Rect effectRack = (Rect){
        .width = effectsTab.width - 4,
        .height = effectsTab.height,
        .x = effectsTab.x + 4,
        .y = effectsTab.y
    };

    Rect effectRackSplitter = (Rect){
        .width = 4,
        .height = effectsTab.height,
        .x = effectsTab.x,
        .y = effectsTab.y
    };

    bool effectsSplitterHover = pointInsideRect(input.mouse_x, input.mouse_y, effectRackSplitter);
    static bool usingEffectsSplitter = false;

    drawSprite((SpriteDrawCommand){
        .position = (vec2){effectRackSplitter.x, effectRackSplitter.y},
        .scale = (vec2){effectRackSplitter.width, effectRackSplitter.height},
        .albedo = effectsSplitterHover || usingEffectsSplitter ? hex2rgb(0xFF909090) : hex2rgb(0xFF101010),
    });

    if(effectsSplitterHover && !usingEffectsSplitter && input.keys[KEY_MOUSE_LEFT].justPressed) usingEffectsSplitter = true;
    if(usingEffectsSplitter && input.keys[KEY_MOUSE_LEFT].justReleased) usingEffectsSplitter = false;
    if(usingEffectsSplitter) effectTabSplitterOffset = swapchainExtent.width - input.mouse_x;


    drawSprite((SpriteDrawCommand){
        .position = (vec2){effectRack.x, effectRack.y},
        .scale = (vec2){effectRack.width, effectRack.height},
        .albedo = (vec3){(float)0x25/255,(float)0x25/255,(float)0x25/255},
    });

    drawSprite((SpriteDrawCommand){
        .position = (vec2){effectRack.x, effectRack.y},
        .scale = (vec2){effectRack.width, UI_FONT_SIZE * 1.5},
        .albedo = hex2rgb(0xFF454545)
    });

    drawSprite((SpriteDrawCommand){
        .position = (vec2){effectRack.x, effectRack.y},
        .scale = (vec2){effectRack.width, UI_FONT_SIZE * 1.5 - 1},
        .albedo = hex2rgb(0xFF181818)
    });

    char* effectsRackStr = "Effects Rack";

    drawText(effectsRackStr, 0xFFFFFFFF, UI_FONT_SIZE, (Rect){
        .x = effectRack.x + effectRack.width/2 - measureText(effectsRackStr, UI_FONT_SIZE)/2,
        .y = effectRack.y + UI_FONT_SIZE*1.5/2 - UI_FONT_SIZE * 0.75,
    });

    Rect vfxContainer = (Rect){
        .x = effectRack.x,
        .y = effectRack.y + UI_FONT_SIZE * 1.5 + 2,
        .width = effectRack.width,
        .height = effectRack.height - UI_FONT_SIZE * 1.5
    };

    drawCurrentModuleInstances(vfxContainer, deltaTime);

    // --------------------------- Timeline --------------------------------

    Rect timelineContainer = (Rect){
        .width = timelineRect.width,
        .height = timelineRect.height - 4,
        .x = timelineRect.x,
        .y = timelineRect.y + 4
    };

    Rect timelineSplitter = (Rect){
        .width = timelineRect.width,
        .height = 4,
        .x = timelineRect.x,
        .y = timelineRect.y
    };

    bool timelineSplitterHover = pointInsideRect(input.mouse_x, input.mouse_y, timelineSplitter);
    static bool usingTimelineSplitter = false;

    drawSprite((SpriteDrawCommand){
        .position = (vec2){timelineSplitter.x, timelineSplitter.y},
        .scale = (vec2){timelineSplitter.width, timelineSplitter.height},
        .albedo = timelineSplitterHover || usingTimelineSplitter ? hex2rgb(0xFF909090) : hex2rgb(0xFF101010),
    });

    if(timelineSplitterHover && !usingTimelineSplitter && input.keys[KEY_MOUSE_LEFT].justPressed) usingTimelineSplitter = true;
    if(usingTimelineSplitter && input.keys[KEY_MOUSE_LEFT].justReleased) usingTimelineSplitter = false;
    if(usingTimelineSplitter) timelineSplitterOffset = swapchainExtent.height - input.mouse_y;

    float percent = videoFrame.frameTime / video.duration;

    float cursorWidth = 1;

    drawSprite((SpriteDrawCommand){
        .position = (vec2){timelineContainer.x, timelineContainer.y},
        .scale = (vec2){timelineContainer.width, timelineContainer.height},
        .albedo = hex2rgb(0xFF181818),
    });
    
    drawSprite((SpriteDrawCommand){
        .position = (vec2){floor(timelineContainer.x+percent*timelineContainer.width+cursorWidth/2),timelineContainer.y},
        .scale = (vec2){cursorWidth, timelineContainer.height},
        .albedo = (vec3){1.0,0.0,0.0},
    });

    if(pointInsideRect(input.mouse_x, input.mouse_y, timelineContainer) && input.keys[KEY_MOUSE_LEFT].justPressed){
        time = ((float)input.mouse_x - timelineContainer.x) * video.duration / timelineContainer.width;
        if(!ffmpegVideoSeek(&video, &videoFrame,time)) return false;
        if(audioInMedia) {
            ffmpegAudioSeek(&audio, time);
            soundEngineSetTime(time);
        }
        if(!playing){ //redraw
            if(ffmpegVideoGetFrame(&video,&videoFrame)){
                for(int i = 0; i < videoFrame.height; i++){
                    memcpy(
                        videoMapped + videoVulkanStride*i,
                        videoFrame.data + videoFrame.width*sizeof(uint32_t)*i,
                        videoFrame.width *sizeof(uint32_t)
                    );
                }
            }
        }
    }

    // -------------------------------- top bar ---------------------------------------
    drawSprite((SpriteDrawCommand){
        .position = (vec2){topBarRect.x, topBarRect.y},
        .scale = (vec2){topBarRect.width, topBarRect.height},
        .albedo = hex2rgb(0xFF181818),
    });

    drawText("FVFX", 0xFFFFFF, UI_FONT_SIZE, (Rect){
        .x = 10,
        .y = 3,
    });

    char text[256];

    sprintf(text, "%.2fs/%.2fs", videoFrame.frameTime, video.duration);

    drawText(text, 0xFFFFFF, UI_FONT_SIZE, (Rect){
        .x = swapchainExtent.width / 2 - measureText(text,UI_FONT_SIZE) / 2,
        .y = 3,
    });

    return true;
}

bool draw(){
    static VkImageMemoryBarrier barrier;
    size_t currentAttachment = 0;
    
    //previewImage pass / vfx pass
    barrier = (VkImageMemoryBarrier){0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = previewImage1;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = previewView1,
        .renderArea = (VkExtent2D){.width = videoFrame.width, .height = videoFrame.height},
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = videoFrame.width,
        .height = videoFrame.height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = (VkExtent2D){.width = videoFrame.width, .height = videoFrame.height},
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineImagePreview);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineImageLayoutPreview,0,1,&previewDescriptorSet,0,NULL);

    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRendering(cmd);

    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    for(size_t i = 0; i < currentModuleInstances.count; i++){
        barrier.image = currentAttachment == 0 ? previewImage2 : previewImage1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, NULL,
            0, NULL,
            1, &barrier
        );

        VfxModule* module = currentModuleInstances.items[i].module;

        vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
            .colorAttachment = currentAttachment == 0 ? previewView2 : previewView1,
            .renderArea = (VkExtent2D){.width = videoFrame.width, .height = videoFrame.height},
        });

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = videoFrame.width,
            .height = videoFrame.height
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent = (VkExtent2D){.width = videoFrame.width, .height = videoFrame.height},
        });

        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, module->pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,module->pipelineLayout,0,1,currentAttachment == 0 ? &outDescriptorSet1 : &outDescriptorSet2,0,NULL);

        if(module->inputs.count > 0){
            vkCmdPushConstants(cmd, module->pipelineLayout, VK_SHADER_STAGE_ALL, 0, module->pushContantsSize, currentModuleInstances.items[i].inputPushConstants);
        }

        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRendering(cmd);

        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, NULL,
            0, NULL,
            1, &barrier
        );

        currentAttachment = 1 - currentAttachment;
    }

    VkImageView swapchainImage = getSwapchainImageView();

    //preview pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = swapchainImage,
        .clearColor = (Color){0,0,0, 1},
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent,
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinePreview);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayoutPreview,0,1,currentAttachment == 0 ? &outDescriptorSet1 : &outDescriptorSet2,0,NULL);
    vkCmdPushConstants(cmd,pipelineLayoutPreview,VK_SHADER_STAGE_ALL,0,sizeof(PushConstantsPreview), &pcsPreview);

    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRendering(cmd);

    //sprite pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = swapchainImage,
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