#include "gui_helpers.h"
#include "engine/input.h"

float UI_FONT_SIZE = 16.0f;

Rect fitRectangle(Rect outer, float innerWidth, float innerHeight){
    Rect out = {0};
    out.x = outer.x;
    out.y = outer.y;

    float innerAspect = (float)innerWidth / (float)innerHeight;
    float outerAspect = outer.width / outer.height;

    if (outerAspect < innerAspect) {
        float scaledH = outer.width / innerAspect;
        float yOffset = (outer.height - scaledH) * 0.5f;

        out.width = outer.width;
        out.height = scaledH;
        out.y += yOffset;
    } else {
        float scaledW = outer.height * innerAspect;
        float xOffset = (outer.width - scaledW) * 0.5f;
        
        out.width = scaledW;
        out.height = outer.height;
        out.x += xOffset;
    }

    return out;
}

#include "modules/bindlessTexturesManager.h"
#include "modules/spriteManager.h"

void hexToRgb(uint32_t hex, float* r, float *g, float *b){
    *b = (float)((hex >> (0*8)) & 0xFF) / 255.0f;
    *g = (float)((hex >> (1*8)) & 0xFF) / 255.0f;
    *r = (float)((hex >> (2*8)) & 0xFF) / 255.0f;
}

void drawText(const char* text, uint32_t color, float fontSize, Rect bounding){
    float r,g,b;
    hexToRgb(color, &r,&g,&b);

    float initialX = bounding.x;
    size_t len = strlen(text);
    for(int i = 0; i < len; i++){
        char ch = text[i];
        if(ch == '\n'){
            bounding.x = initialX;
            bounding.y += fontSize;
        }
        if ((unsigned char)ch >= GLYPH_METRICS_CAPACITY) ch = '?';
        
        GlyphMetric metric = atlas.glyphMetrics[ch];

        float x2 = bounding.x + metric.bl * fontSize / FREE_GLYPH_FONT_SIZE;
        float y2 = bounding.y + fontSize - metric.bt * fontSize / FREE_GLYPH_FONT_SIZE;

        float w = metric.bw * fontSize / FREE_GLYPH_FONT_SIZE;
        float h = metric.bh * fontSize / FREE_GLYPH_FONT_SIZE;

        bounding.x += metric.ax * fontSize / FREE_GLYPH_FONT_SIZE;
        bounding.y += metric.ay * fontSize / FREE_GLYPH_FONT_SIZE;

        drawSprite((SpriteDrawCommand){
            .position = (vec2){x2,y2},
            .scale = (vec2){w,h},
            .textureIDEffects = getTextureID("font") | TEXTURE_EFFECT_SDF,
            .offset = (vec2){.x = metric.tx, .y = 0},
            .size = (vec2){
                .x = metric.bw / (float)atlas.width,
                .y = metric.bh / (float) atlas.height
            },
            .albedo = (vec3){r,g,b},
        });
    }
}

float measureText(const char* text, float fontSize){
    float width = 0;
    float newWidth = 0;

    size_t len = strlen(text);
    for(int i = 0; i < len; i++){
        char ch = text[i];
        if(ch == '\n'){
            if (newWidth > width) width = newWidth;
            newWidth = 0;
            continue;        
        }

        GlyphMetric metric = atlas.glyphMetrics[ch];
        newWidth += metric.ax * fontSize / FREE_GLYPH_FONT_SIZE;
    }

    if (newWidth > width) width = newWidth;

    return width;
}

bool pointInsideRect(float x, float y, Rect rect){
    return !(
        (x < rect.x) ||
        (x > rect.x + rect.width) ||
        (y < rect.y) ||
        (y > rect.y + rect.height)
    );
}

bool rectIntersectsRect(Rect a, Rect b) {
    return !(
        a.x + a.width < b.x ||
        a.x > b.x + b.width ||
        a.y + a.height < b.y ||
        a.y > b.y + b.height
    );
}

vec3 hex2rgb(uint32_t hex){
    return (vec3){
        (float)((hex >> 8 * 2) & 0xFF) / 255.0f,
        (float)((hex >> 8 * 1) & 0xFF) / 255.0f,
        (float)((hex >> 8 * 0) & 0xFF) / 255.0f,
    };
}

bool drawButton_internal(Rect boundingBox, const char* text, uint32_t GUID){
    bool hover = pointInsideRect(input.mouse_x, input.mouse_y,  boundingBox);

    drawSprite((SpriteDrawCommand){
        .position = (vec2){boundingBox.x, boundingBox.y},
        .scale = (vec2){boundingBox.width, boundingBox.height},
        .albedo = hex2rgb(0xFF352020)
    });

    size_t border = 1;

    drawSprite((SpriteDrawCommand){
        .position = (vec2){boundingBox.x + border, boundingBox.y + border},
        .scale = (vec2){boundingBox.width - border*2, boundingBox.height - border*2},
        .albedo = input.keys[KEY_MOUSE_LEFT].isDown && hover ? hex2rgb(0xFF753030) : (hover ? hex2rgb(0xFF653030) : hex2rgb(0xFF503030)),
    });

    Rect textRect = (Rect){
        .x = boundingBox.x + boundingBox.width / 2 - measureText(text, UI_FONT_SIZE)/2,
        .y = boundingBox.y + boundingBox.height / 2 - UI_FONT_SIZE * 0.75,
    };

    drawText(text, 0xFFFFFFFF, UI_FONT_SIZE, textRect);

    if(hover && input.keys[KEY_MOUSE_LEFT].justReleased) return true;

    return false;
}

size_t editingGUID = -1;

float origFloatEditVal;
vec2 startEditPos;

void drawFloatBox_internal(Rect boundingBox, float* val, uint32_t GUID){
    if(editingGUID != -1 && input.keys[KEY_MOUSE_LEFT].justReleased) editingGUID = -1;

    bool hover = pointInsideRect(input.mouse_x, input.mouse_y, boundingBox);
    if(editingGUID == -1 && hover && input.keys[KEY_MOUSE_LEFT].justPressed){
        editingGUID = GUID;
        startEditPos = (vec2){input.mouse_x, input.mouse_y};
        origFloatEditVal = *val;
    }

    if(editingGUID == GUID){
        *val = origFloatEditVal + (float)(input.mouse_x - startEditPos.x) / 100.0f;
    }

    drawSprite((SpriteDrawCommand){
        .position = (vec2){boundingBox.x, boundingBox.y},
        .scale = (vec2){boundingBox.width, boundingBox.height},
        .albedo = hex2rgb(0xFF203520)
    });

    size_t border = 1;

    drawSprite((SpriteDrawCommand){
        .position = (vec2){boundingBox.x + border, boundingBox.y + border},
        .scale = (vec2){boundingBox.width - border*2, boundingBox.height - border*2},
        .albedo = editingGUID == GUID ? hex2rgb(0xFF309530) : (hover ? hex2rgb(0xFF306530) : hex2rgb(0xFF305030)),
    });

    const char* text = temp_sprintf("%.02ff", *val);

    Rect textRect = (Rect){
        .x = boundingBox.x + boundingBox.width / 2 - measureText(text, UI_FONT_SIZE)/2,
        .y = boundingBox.y + boundingBox.height / 2 - UI_FONT_SIZE * 0.75,
    };

    drawText(text, 0xFFFFFFFF, UI_FONT_SIZE, textRect);
}