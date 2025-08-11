#include "gui_helpers.h"
#include "engine/input.h"
#include <math.h>

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

void drawText(const char* text, uint32_t color, float fontSize, Rect bounding){
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
            .albedo = hex2rgb(color),
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

vec3 hsv2rgb(vec3 hsv) {
    float h = hsv.x;
    float s = hsv.y;
    float v = hsv.z;

    float c = v * s; // Chroma
    float hPrime = h * 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hPrime, 2.0f) - 1.0f));

    float r = 0, g = 0, b = 0;

    if (0 <= hPrime && hPrime < 1) {
        r = c; g = x; b = 0;
    } else if (1 <= hPrime && hPrime < 2) {
        r = x; g = c; b = 0;
    } else if (2 <= hPrime && hPrime < 3) {
        r = 0; g = c; b = x;
    } else if (3 <= hPrime && hPrime < 4) {
        r = 0; g = x; b = c;
    } else if (4 <= hPrime && hPrime < 5) {
        r = x; g = 0; b = c;
    } else if (5 <= hPrime && hPrime < 6) {
        r = c; g = 0; b = x;
    }

    float m = v - c;
    return (vec3){ r + m, g + m, b + m };
}

vec3 rgb2hsv(vec3 rgb) {
    float r = rgb.x;
    float g = rgb.y;
    float b = rgb.z;

    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float delta = max - min;

    float h = 0.0f;
    if (delta > 0.00001f) {
        if (max == r) {
            h = fmodf(((g - b) / delta), 6.0f);
        } else if (max == g) {
            h = ((b - r) / delta) + 2.0f;
        } else {
            h = ((r - g) / delta) + 4.0f;
        }
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }

    float s = (max == 0.0f) ? 0.0f : (delta / max);
    float v = max;

    return (vec3){ h, s, v };
}

size_t currentGUID = 0;

size_t editingGUID = -1;
float origFloatEditVal;
vec2 startEditPos;

void ui_begin(){
    currentGUID = 0;
}

void ui_reset(){
    editingGUID = -1;
}

void ui_end(){

}

bool drawButton(Rect boundingBox, const char* text){
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

    currentGUID++;

    if(hover && input.keys[KEY_MOUSE_LEFT].justReleased) return true;

    return false;
}

void drawFloatBox(Rect boundingBox, float* val){
    if(editingGUID != -1 && input.keys[KEY_MOUSE_LEFT].justReleased) editingGUID = -1;

    bool hover = pointInsideRect(input.mouse_x, input.mouse_y, boundingBox);
    if(editingGUID == -1 && hover && input.keys[KEY_MOUSE_LEFT].justPressed){
        editingGUID = currentGUID;
        startEditPos = (vec2){input.mouse_x, input.mouse_y};
        origFloatEditVal = *val;
    }

    if(editingGUID == currentGUID){
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
        .albedo = editingGUID == currentGUID ? hex2rgb(0xFF309530) : (hover ? hex2rgb(0xFF306530) : hex2rgb(0xFF305030)),
    });

    const char* text = temp_sprintf("%.02ff", *val);

    Rect textRect = (Rect){
        .x = boundingBox.x + boundingBox.width / 2 - measureText(text, UI_FONT_SIZE)/2,
        .y = boundingBox.y + boundingBox.height / 2 - UI_FONT_SIZE * 0.75,
    };

    drawText(text, 0xFFFFFFFF, UI_FONT_SIZE, textRect);

    currentGUID++;
}

float expDecay(float a, float b, float decay, float deltaTime){
    return b + (a - b) * expf(-decay*deltaTime);
}