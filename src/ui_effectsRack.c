#include "ui_effectsRack.h"
#include "gui_helpers.h"
#include "engine/input.h"
#include "modules/spriteManager.h"
#include "modules/bindlessTexturesManager.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "vulkan/vulkan.h"
#include "vfx.h"

#define EFFECT_RACK_OFFSET_BETWEEN_INSTANCES 2

static SpriteDrawCommands tempDrawQueue = {0};

static float scrollOffset = 0;

static float targetScrollOffset = 0;

void drawCurrentModuleInstances(VfxInstances* currentModuleInstances, Rect vfxContainer,float deltaTime){
    size_t savedCurrentModuleInstancesCount = currentModuleInstances->count;
    if(savedCurrentModuleInstancesCount > 0) beginScissor(vfxContainer.x, vfxContainer.y, vfxContainer.width, vfxContainer.height);

    const float ContainerHeight = UI_FONT_SIZE * 1.5;
    const float ContainerWidth = vfxContainer.width;
    
    if(targetScrollOffset > 0){
        targetScrollOffset = expDecay(targetScrollOffset, 0, 8, deltaTime);
    }
    scrollOffset = expDecay(scrollOffset, targetScrollOffset, 8, deltaTime);

    float offset = 0;

    float lastElementSize = 0;

    for(size_t i = 0; i < currentModuleInstances->count; i++){
        VfxInstance* instance = &currentModuleInstances->items[i];

        const float moduleX = vfxContainer.x;
        const float moduleY = vfxContainer.y + offset + scrollOffset;

        Rect moduleRect = (Rect){
            .x = moduleX,
            .y = moduleY,
            .width = ContainerWidth,
            .height = ContainerHeight
        };

        bool hovering = pointInsideRect(input.mouse_x, input.mouse_y, moduleRect);

        if(((input.keys[KEY_CONTROL].isDown && input.scroll > 0) || input.keys[KEY_UP].justReleased) && i > 0 && hovering){
            VfxInstance copy = *instance;
            memcpy(&currentModuleInstances->items[i], &currentModuleInstances->items[i - 1], sizeof(VfxInstance));
            memcpy(&currentModuleInstances->items[i - 1], &copy, sizeof(VfxInstance));
        }

        if(((input.keys[KEY_CONTROL].isDown && input.scroll < 0) || input.keys[KEY_DOWN].justReleased) && i < currentModuleInstances->count-1 && hovering){
            VfxInstance copy = *instance;
            memcpy(&currentModuleInstances->items[i], &currentModuleInstances->items[i + 1], sizeof(VfxInstance));
            memcpy(&currentModuleInstances->items[i + 1], &copy, sizeof(VfxInstance));
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
            .albedo = hoverDelete ? hex2rgb(0xFFff3f3f) : hex2rgb(0xFFFFFFFF)
        });

        if(instance->module->inputs.count > 0){
            if(input.keys[KEY_MOUSE_LEFT].justReleased && hovering && !hoverDelete) instance->opened = !instance->opened;
        }

        if((input.keys[KEY_MOUSE_LEFT].justReleased && hoverDelete) || (input.keys[KEY_DELETE].justReleased && hovering)) da_remove_at(currentModuleInstances, i);

        offset += ContainerHeight;

        lastElementSize = ContainerHeight;

        if(instance->opened && instance->module->inputs.count > 0){
            tempDrawQueue.count = 0;
            redirectDrawSprites(&tempDrawQueue);

            float openSize = 0;

            Rect openRect = (Rect){
                .x = moduleRect.x,
                .y = moduleRect.y + moduleRect.height,
                .width = moduleRect.width,
            };

            size_t byteOffset = 0;

            for(size_t j = 0; j < instance->module->inputs.count; j++){
                float inputY = openRect.y + 1 + openSize;

                VfxInput* inputVFX = &instance->module->inputs.items[j];

                float textY = inputY;

                const float inputWidth = min(openRect.width - measureText(inputVFX->name, UI_FONT_SIZE), openRect.width * 0.65);
                const float inputHeight = UI_FONT_SIZE * 1.5;

                switch (inputVFX->type)
                {
                case VFX_FLOAT:
                    {
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth,
                            .y = inputY,
                            .width = inputWidth,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset));

                        openSize += inputHeight;
                        break;
                    }
                
                case VFX_VEC2:
                    {
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth,
                            .y = inputY,
                            .width = inputWidth/2,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset));

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth/2,
                            .y = inputY,
                            .width = inputWidth/2,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float)));

                        openSize += inputHeight;
                        break;
                    }

                case VFX_VEC3:
                    {
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth,
                            .y = inputY,
                            .width = inputWidth/3,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset));
                        
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth + inputWidth/3,
                            .y = inputY,
                            .width = inputWidth/3,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float)));

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth/3,
                            .y = inputY,
                            .width = inputWidth/3,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float) * 2));

                        openSize += inputHeight;
                        break;
                    }

                case VFX_VEC4:
                    {
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth,
                            .y = inputY,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset));
                        
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth + inputWidth/4,
                            .y = inputY,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float)));

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth + inputWidth * 2 /4,
                            .y = inputY,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float) * 2));

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth/4,
                            .y = inputY,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float) * 3));

                        openSize += inputHeight;
                        break;
                    }

                case VFX_COLOR: 
                    {
                        vec3* hsv = (vec3*)(instance->inputPushConstants + byteOffset);

                        const float padding = 10;

                        float finalHeight = 0;
                        float width = min(inputWidth - padding, 200);
                        float height = width;

                        Rect colorPicker = (Rect){
                            .x = openRect.x + openRect.width - width - padding/2,
                            .y = inputY + padding/2,
                            .width = width,
                            .height = height,
                        };

                        if(pointInsideRect(input.mouse_x, input.mouse_y, colorPicker) && input.keys[KEY_MOUSE_LEFT].isDown){
                            hsv->y = ((float)input.mouse_x - colorPicker.x) / colorPicker.width;
                            hsv->z = 1.0f - ((float)input.mouse_y - colorPicker.y) / colorPicker.height;
                        }

                        drawSprite((SpriteDrawCommand){
                            .position = (vec2){colorPicker.x, colorPicker.y},
                            .scale = (vec2){colorPicker.width, colorPicker.height},
                            .albedo = (vec3){hsv->x},
                            .textureIDEffects = TEXTURE_EFFECT_HSV_GRADIENT,
                        });

                        float pickerSize = 12.5;

                        drawSprite((SpriteDrawCommand){
                            .position = (vec2){
                                colorPicker.x + hsv->y * colorPicker.width - pickerSize/2,
                                colorPicker.y + (1.0f - hsv->z) * colorPicker.height - pickerSize/2,
                            },
                            .scale = (vec2){
                                pickerSize, pickerSize
                            },
                            .textureIDEffects = getTextureID("assets/ColorPicker.png"),
                        });

                        finalHeight += height + padding;

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth,
                            .y = inputY + finalHeight,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, &hsv->x);
                        
                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth + inputWidth/4,
                            .y = inputY + finalHeight,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, &hsv->y);

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth + inputWidth * 2 /4,
                            .y = inputY + finalHeight,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, &hsv->z);

                        drawFloatBox((Rect){
                            .x = openRect.x + openRect.width - inputWidth/4,
                            .y = inputY + finalHeight,
                            .width = inputWidth/4,
                            .height = inputHeight,
                        }, (float*)((char*)instance->inputPushConstants + byteOffset + sizeof(float) * 3));

                        finalHeight += inputHeight;

                        openSize += finalHeight;
                        textY = inputY + finalHeight / 2 - UI_FONT_SIZE * 0.75;
                        break;
                    }

                default: UNREACHABLE("Implement This!");
                }

                drawText(inputVFX->name, 0xFFFFFFFF, UI_FONT_SIZE, (Rect){
                    .x = openRect.x,
                    .y = textY,
                });

                byteOffset += get_vfxInputTypeSize(inputVFX->type);
            }

            redirectDrawSprites(NULL);

            drawSprite((SpriteDrawCommand){
                .position = (vec2){openRect.x, openRect.y},
                .scale = (vec2){openRect.width, openSize},
                .albedo = hex2rgb(0xFF909090)
            });

            drawSprite((SpriteDrawCommand){
                .position = (vec2){openRect.x, openRect.y + 1},
                .scale = (vec2){openRect.width, openSize - 1},
                .albedo = hex2rgb(0xFF404040)
            });

            drawSprites(&tempDrawQueue);

            offset += openSize;
            lastElementSize += openSize;
        }

        offset += EFFECT_RACK_OFFSET_BETWEEN_INSTANCES;
    }

    bool largeEnough = offset > vfxContainer.height;

    if(!input.keys[KEY_CONTROL].isDown && pointInsideRect(input.mouse_x, input.mouse_y, vfxContainer) && input.scroll != 0){
        if(largeEnough){
            targetScrollOffset += (float)input.scroll * 0.2;
        }else if(input.scroll > 0){
            targetScrollOffset += (float)input.scroll * 0.2;
        }
    }

    if(largeEnough && -targetScrollOffset > offset - vfxContainer.height){
        targetScrollOffset = expDecay(targetScrollOffset, -( offset - vfxContainer.height), 8, deltaTime);
    }else if(!largeEnough && targetScrollOffset < 0){
        targetScrollOffset = expDecay(targetScrollOffset, 0, 8, deltaTime);
    }

    if(savedCurrentModuleInstancesCount > 0) endScissor();
}

static String_Builder sb = {0};
extern VkDescriptorSetLayout vfxDescriptorSetLayout;
static VkShaderModule vfxFragmentShader;
static VkShaderModule vfxVertexShader;

bool addEffectsToRack(VfxInstances* currentModuleInstances, Hashmap* vfxModulesHashMap, const char** dragndrop, int count){
    for(int i = 0; i < count; i++){
    
        HashItem* item = getFromHashMap(vfxModulesHashMap, dragndrop[i]);
        if(item == NULL){
            printf("UNREACHABLE!\n");
            return false;
        }

        if(item->value.filepath == NULL){
            item->value = (VfxModule){0};
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
            
            if(!compileShader(sb.items,shaderc_fragment_shader,&vfxFragmentShader)) return false;
            
            size_t pushContantsSize = 0;
            for(size_t i = 0; i < item->value.inputs.count; i++){
                pushContantsSize += get_vfxInputTypeSize(item->value.inputs.items[i].type);
            }
            item->value.pushContantsSize = pushContantsSize;

            if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
                .vertexShader = vfxVertexShader,
                .fragmentShader = vfxFragmentShader,
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
            if(instance.module->defaultPushConstantValue != NULL){
                memcpy(instance.inputPushConstants, instance.module->defaultPushConstantValue, instance.module->pushContantsSize);
            }
        }
        
        da_append(currentModuleInstances, instance);
    }

    return true;
}

void drawEffectsRack(float deltaTime, VfxInstances* currentModuleInstances, Rect effectRack){
    drawSprite((SpriteDrawCommand){
        .position = (vec2){effectRack.x, effectRack.y},
        .scale = (vec2){effectRack.width, effectRack.height},
        .albedo = hex2rgb(0xFF252525),
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

    drawCurrentModuleInstances(currentModuleInstances, vfxContainer, deltaTime);
}