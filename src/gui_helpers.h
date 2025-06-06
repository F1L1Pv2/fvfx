#ifndef FVFX_GUI_HELPERS
#define FVFX_GUI_HELPERS

typedef struct{
    float x;
    float y;
    float width;
    float height;
} Rect;

Rect fitRectangle(Rect outer, float innerWidth, float innerHeight);

#endif