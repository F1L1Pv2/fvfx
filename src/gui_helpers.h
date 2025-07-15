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

void ui_begin();
void ui_reset();
void ui_end();

bool drawButton(Rect boundingBox, const char* text);
void drawFloatBox(Rect boundingBox, float* val);


#endif