#import <Foundation/Foundation.h>
#include "EngineVisionBridge.h"
#include "metalrenderer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void enginepad_prepare(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        /* Flat iPad rendering needs one camera. Keep the shared visionOS
         * runtime and its stereo/panorama defaults unchanged. */
        const char *settings[][2] = {
            {"HALO_FF3D", "1"}, {"HALO_PANORAMA", "0"},
            {"HALO_PANORAMA_GPU", "0"}, {"HALO_STEREO", "0"},
            {"HALO_SPATIAL_MENU", "0"}, {"HALO_GAZE_POINTER", "0"},
            {"HALO_PV_SKIN_FAST", "1"}, {"HALO_NATIVE_GATHER", "1"},
            {"HALO_DRAW_FASTPATH", "1"}, {"HALO_PAD2KEY", "1"},
            {"HALO_A10_AUTOPLAY", "0"}, {"HALO_UNLOCK_CAMPAIGN", "1"}
        };
        for (size_t i=0; i<sizeof settings/sizeof settings[0]; ++i)
            setenv(settings[i][0], settings[i][1], 0);
        const char *fast=getenv("HALO_DRAW_FASTPATH");
        mr_set_fast_paths(fast && !strcmp(fast,"1"));
        enginevision_prepare_controller();
    });
}

int enginepad_start(const char *game_root) {
    enginepad_prepare();
    NSUserDefaults *preferences=NSUserDefaults.standardUserDefaults;
    NSArray<NSString *> *resolutions=@[@"1280x960", @"1600x1200", @"1920x1440", @"2048x1536"];
    NSString *selected=[preferences stringForKey:@"HaloPadRenderResolution"];
    if (![resolutions containsObject:selected]) selected=@"1600x1200";
    NSString *mode=[selected stringByReplacingOccurrencesOfString:@"x" withString:@","];
    const char *explicitMode=getenv("HALO_VIDMODE");
    char command[128];
    snprintf(command,sizeof command,"-vidmode %s,60",mode.UTF8String);
    if (explicitMode && explicitMode[0]) snprintf(command,sizeof command,"-vidmode %s",explicitMode);
    setenv("HALO_CMDLINE_EXTRA",command,0);
    NSDictionary<NSString *, NSString *> *packs=@{@"TextureMods":@"hvt", @"ShaderMods":@"hvs"};
    for (NSString *name in packs) {
        NSString *path=[NSBundle.mainBundle pathForResource:name ofType:packs[name]];
        if (path) setenv([name isEqualToString:@"TextureMods"]?"HALO_TEXTURE_PACK":"HALO_SHADER_PACK",path.fileSystemRepresentation,0);
    }
    for (NSArray<NSString *> *setting in @[@[@"HaloTextureModsEnabled", @"HALO_TEXTURE_MODS"],
                                           @[@"HaloShaderModsEnabled", @"HALO_SHADER_MODS"]]) {
        id saved=[preferences objectForKey:setting[0]];
        setenv(setting[1].UTF8String,(!saved || [saved boolValue])?"1":"0",0);
    }
    return enginevision_start(game_root);
}
