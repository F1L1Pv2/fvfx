#include "gui_helpers.h"

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