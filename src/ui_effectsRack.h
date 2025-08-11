#ifndef FVFX_UI_EFFECTS_RACK
#define FVFX_UI_EFFECTS_RACK

#include "gui_helpers.h"
#include "vfx.h"

void drawEffectsRack(float deltaTime, VfxInstances* currentModuleInstances, Rect effectRack);
bool addEffectsToRack(VfxInstances* currentModuleInstances, Hashmap* vfxModulesHashMap, const char** dragndrop, int count);

#endif