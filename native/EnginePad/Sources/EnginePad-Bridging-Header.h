#include "EngineVisionBridge.h"
#include "gamecontroller.h"
#include "device_input.h"
#include "haptics.h"
#include "halo_settings.h"

/* Configure flat rendering before anything initializes the engine settings. */
void enginepad_prepare(void);
int enginepad_start(const char *game_root);
