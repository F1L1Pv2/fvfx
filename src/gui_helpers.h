#ifndef FVFX_GUI_HELPERS
#define FVFX_GUI_HELPERS

#include <stdlib.h>
#include <stdbool.h>
#include "modules/font_freetype.h"

extern GlyphAtlas atlas;

typedef struct{
    float x;
    float y;
    float width;
    float height;
} Rect;

Rect fitRectangle(Rect outer, float innerWidth, float innerHeight);

void drawText(const char* text, uint32_t color, float fontSize, Rect bounding);
float measureText(const char* text, float fontSize);
bool pointInsideRect(float x, float y, Rect rect);
bool rectIntersectsRect(Rect a, Rect b);

extern float UI_FONT_SIZE;

vec3 hex2rgb(uint32_t hex);

bool drawButton_internal(Rect boundingBox, const char* text, uint32_t GUID);
#define drawButton(boundingBox, text) drawButton_internal((boundingBox), (text), ((size_t)__FILE__)+__LINE__)
void drawFloatBox_internal(Rect boundingBox, float* val, uint32_t GUID);
#define drawFloatBox(boundingBox, val) drawFloatBox_internal((boundingBox), (val), ((size_t)__FILE__)+__LINE__)


#endif