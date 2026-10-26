/* Exercise the same Double-degrees -> Float-radians conversion as Swift's
 * controls. Endpoint checks cover both the live setter and cached accessor;
 * each environment case begins in a child before the parent's initialization.
 * clang -O2 -pthread tests/test_halo_settings_fov.c halo_settings.c -lm -o /tmp/fov */
#include "../halo_settings.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static float radians(double degrees) {
    return (float)(degrees * 3.14159265358979323846 / 180.0);
}

static void check_environment(const char *value, double expected) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        assert(!setenv("HALO_PANORAMA_VFOV", value, 1));
        HaloSettings settings;
        halo_settings_get(&settings);
        assert(settings.panorama_vfov == radians(expected));
        assert(halo_settings_panorama_vfov() == radians(expected));
        /* SettingsModel's initial push must preserve the environment endpoint. */
        halo_settings_set(&settings);
        assert(halo_settings_panorama_vfov() == radians(expected));
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void check_live(float value, float expected) {
    HaloSettings before, after;
    halo_settings_get(&before);
    HaloSettings changed = before;
    changed.panorama_vfov = value;
    halo_settings_set(&changed);
    halo_settings_get(&after);
    assert(after.panorama_vfov == expected);
    assert(halo_settings_panorama_vfov() == expected);
    assert(after.stereo_separation == before.stereo_separation);
    assert(after.haptics_strength == before.haptics_strength);
}

int main(void) {
    check_environment("60", 60);
    check_environment("175", 175);
    check_environment("nan", 105);
    check_environment("176", 105);
    unsetenv("HALO_PANORAMA_VFOV");
    check_live(radians(60), radians(60));
    check_live(radians(175), radians(175));
    for (int degrees = 90; degrees <= 175; degrees += 5)
        check_live(radians(degrees), radians(degrees));
    const float fallback = 2.6179939f;
    check_live(nextafterf(radians(60), -INFINITY), fallback);
    check_live(nextafterf(radians(175), INFINITY), fallback);
    check_live(NAN, fallback);
    check_live(INFINITY, fallback);
    check_live(-INFINITY, fallback);
    puts("PASS FOV: exact 60/175-degree environment and live endpoints, every slider step, adjacent invalid values, NaN/infinity fallback");
    return 0;
}
