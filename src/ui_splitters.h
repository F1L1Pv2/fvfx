#ifndef FVFX_UI_SPLITTERS
#define FVFX_UI_SPLITTERS

#include "stdbool.h"
#include "gui_helpers.h"

void drawHorizontalSplitter(Rect boundingBox, bool *usingSplitter, float* splitterOffset);
void drawVerticalSplitter(Rect boundingBox, bool *usingSplitter, float* splitterOffset);

#endif