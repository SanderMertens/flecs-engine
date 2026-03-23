#include <math.h>
#include <stdlib.h>

#include "../renderer.h"
#include "hdri_loader.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

bool flecsIblBuildDefaultImage(
    const flecs_rgba_t *sky_color,
    const flecs_rgba_t *ground_color,
    const flecs_rgba_t *haze_color,
    const flecs_rgba_t *horizon_color,
    FlecsHdriImage *image)
{
    /* Build a 1×N equirectangular fallback that mimics a simple outdoor
     * environment:
     *  - Sky gradient: deeper at zenith, lighter near the horizon
     *  - Ground gradient: configured color at horizon, darker at nadir
     *  - Horizon glow: subtle bright band at the sky/ground boundary
     *
     * haze_color:    the color the sky lerps toward near the horizon
     *                (defaults to white if zero/not provided)
     * horizon_color: the color of the bright horizon glow band
     *                (defaults to luminance-scaled white if zero/not provided)
     */
    const int fallback_h = 1024;
    image->width = 1;
    image->height = fallback_h;
    image->pixels_rgba32f = malloc(sizeof(float) * 4u * (size_t)fallback_h);
    if (!image->pixels_rgba32f) {
        return false;
    }

    float sky_r = flecsEngine_colorChannelToFloat(sky_color->r);
    float sky_g = flecsEngine_colorChannelToFloat(sky_color->g);
    float sky_b = flecsEngine_colorChannelToFloat(sky_color->b);
    float gnd_r = flecsEngine_colorChannelToFloat(ground_color->r);
    float gnd_g = flecsEngine_colorChannelToFloat(ground_color->g);
    float gnd_b = flecsEngine_colorChannelToFloat(ground_color->b);

    /* Haze target color. */
    float haze_tr = 0.0f, haze_tg = 0.0f, haze_tb = 0.0f;
    if (haze_color) {
        haze_tr = flecsEngine_colorChannelToFloat(haze_color->r);
        haze_tg = flecsEngine_colorChannelToFloat(haze_color->g);
        haze_tb = flecsEngine_colorChannelToFloat(haze_color->b);
    }

    /* Horizon glow color. */
    float hz_r = 0.0f, hz_g = 0.0f, hz_b = 0.0f;
    if (horizon_color) {
        hz_r = flecsEngine_colorChannelToFloat(horizon_color->r);
        hz_g = flecsEngine_colorChannelToFloat(horizon_color->g);
        hz_b = flecsEngine_colorChannelToFloat(horizon_color->b);
    }

    for (int row = 0; row < fallback_h; row++) {
        /* v goes from 0 (zenith) to 1 (nadir), 0.5 = horizon */
        float v = ((float)row + 0.5f) / (float)fallback_h;
        float *px = &image->pixels_rgba32f[row * 4];

        if (v < 0.5f) {
            /* Sky: lerp toward haze color near horizon.
             * haze_start: fraction of the sky half where haze begins
             *   0.0 = haze across entire sky, 1.0 = no haze at all
             *   0.7 = pure sky color in the top 70%, haze in bottom 30% */
            float haze_start = 0.7f;
            float t = v / 0.5f;
            float haze = (t > haze_start)
                ? (t - haze_start) / (1.0f - haze_start) : 0.0f;
            haze = haze * haze;           /* ease-in within the haze zone */
            float haze_strength = 0.35f;  /* how far toward haze color */
            float h = haze * haze_strength;
            px[0] = sky_r + (haze_tr - sky_r) * h;
            px[1] = sky_g + (haze_tg - sky_g) * h;
            px[2] = sky_b + (haze_tb - sky_b) * h;
        } else {
            /* Ground: configured color at the horizon, fading darker
             * toward the nadir.  t=0 at horizon, t=1 at nadir. */
            float t = (v - 0.5f) / 0.5f;
            float darken = t * t;            /* ease-in: gradual near horizon */
            float darken_strength = 0.55f;   /* how much darker at nadir */
            float d = 1.0f - darken * darken_strength;
            px[0] = gnd_r * d;
            px[1] = gnd_g * d;
            px[2] = gnd_b * d;
        }

        /* Horizon glow: bright band at v≈0.5, gaussian-ish falloff */
        float dist = (v - 0.5f) * (float)fallback_h; /* pixels from horizon */
        float glow_width = 3.0f;
        float glow = expf(-(dist * dist) / (2.0f * glow_width * glow_width));
        float glow_strength = 0.3f;
        px[0] += glow * hz_r * glow_strength;
        px[1] += glow * hz_g * glow_strength;
        px[2] += glow * hz_b * glow_strength;

        px[3] = 1.0f;
    }

    return true;
}
