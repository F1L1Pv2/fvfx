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

void drawText(char* text, uint32_t color, float fontSize, Rect bounding);
float measureText(char* text, float fontSize);
bool pointInsideRect(float x, float y, Rect rect);

#define UI_FONT_SIZE (16.0f)

vec3 hex2rgb(uint32_t hex);

void updateUI();
bool drawButton_internal(Rect boundingBox, const char* text, uint32_t GUID);
#define drawButton(boundingBox, text) drawButton_internal((boundingBox), (text), ((size_t)__FILE__)+__LINE__)


#endif