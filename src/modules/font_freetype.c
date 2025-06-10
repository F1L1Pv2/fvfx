#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_buffer.h"

#include "engine/vulkan_images.h"
#include "engine/vulkan_helpers.h"

#include "font_freetype.h"

#include <ft2build.h>
#include FT_FREETYPE_H

FT_Library library = NULL;

bool GetFontSDFAtlas(const char* filename, VkImage* image, VkDeviceMemory* imageMemory, VkImageView* imageView, GlyphAtlas* glyphAtlas){
    if(library == NULL){
        if(FT_Init_FreeType(&library) != 0) return false;
    }

    FT_Face face;
    if(FT_New_Face(library,filename,0,&face) != 0) return false;

    if (FT_Set_Pixel_Sizes(face, 0, FREE_GLYPH_FONT_SIZE) != 0) {
        fprintf(stderr, "ERROR: Could not set pixel size to %u\n", FREE_GLYPH_FONT_SIZE);
        return 1;
    }

    FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_(FT_RENDER_MODE_SDF);
    for (int i = 32; i < 128; ++i) {
        if (FT_Load_Char(face, i, load_flags) != 0) {
            fprintf(stderr, "ERROR: could not load glyph of a character with code %d\n", i);
            return false;
        }

        glyphAtlas->width += face->glyph->bitmap.width;
        if (glyphAtlas->height < face->glyph->bitmap.rows) {
            glyphAtlas->height = face->glyph->bitmap.rows;
        }
    }

    if(!createImage(glyphAtlas->width,glyphAtlas->height,VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, image, imageMemory)){
        return false;
    }

    VkImageSubresource subResource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout subResourceLayout;
    vkGetImageSubresourceLayout( device, *image, &subResource, &subResourceLayout);
    int vulkanImageRowPitch = subResourceLayout.rowPitch;

    void *mapped;
    
    vkMapMemory(device, *imageMemory, 0, vulkanImageRowPitch*glyphAtlas->height, 0, &mapped);
    
    int x = 0;
    for (int i = 32; i < 128; ++i) {
        if (FT_Load_Char(face, i, load_flags) != 0) {
            fprintf(stderr, "ERROR: could not load glyph of a character with code %d\n", i);
            return false;
        }

        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            fprintf(stderr, "ERROR: could not render glyph of a character with code %d\n", i);
            return false;
        }

        glyphAtlas->glyphMetrics[i].ax = face->glyph->advance.x >> 6;
        glyphAtlas->glyphMetrics[i].ay = face->glyph->advance.y >> 6;
        glyphAtlas->glyphMetrics[i].bw = face->glyph->bitmap.width;
        glyphAtlas->glyphMetrics[i].bh = face->glyph->bitmap.rows;
        glyphAtlas->glyphMetrics[i].bl = face->glyph->bitmap_left;
        glyphAtlas->glyphMetrics[i].bt = face->glyph->bitmap_top;
        glyphAtlas->glyphMetrics[i].tx = (float) x / (float) glyphAtlas->width;

        for(int j = 0; j < face->glyph->bitmap.rows; j++){
            memcpy(
                mapped + vulkanImageRowPitch * j + x,
                face->glyph->bitmap.buffer + face->glyph->bitmap.width * j,
                face->glyph->bitmap.width 
            );
        }

        x += face->glyph->bitmap.width;
    }
        
    vkUnmapMemory(device, *imageMemory);
    
    //move it co correct barrier
    VkCommandBuffer tempCmd = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        tempCmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );
    endSingleTimeCommands(tempCmd);
        
    if(!createImageView(*image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, imageView)){
        return false;
    }

    if(FT_Done_Face(face) != 0) return false;

    return true;
}