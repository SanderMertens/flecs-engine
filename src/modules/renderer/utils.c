#include "renderer.h"

static float flecsEngineColorChannelToFloat(
    uint8_t value)
{
    return (float)value / 255.0f;
}

WGPUColor flecsEngineGetClearColor(
    const FlecsEngineImpl *impl)
{
    return (WGPUColor){
        .r = (double)flecsEngineColorChannelToFloat(impl->clear_color.r),
        .g = (double)flecsEngineColorChannelToFloat(impl->clear_color.g),
        .b = (double)flecsEngineColorChannelToFloat(impl->clear_color.b),
        .a = (double)flecsEngineColorChannelToFloat(impl->clear_color.a)
    };
}

void flecsEngineGetClearColorVec4(
    const FlecsEngineImpl *impl,
    float out[4])
{
    out[0] = flecsEngineColorChannelToFloat(impl->clear_color.r);
    out[1] = flecsEngineColorChannelToFloat(impl->clear_color.g);
    out[2] = flecsEngineColorChannelToFloat(impl->clear_color.b);
    out[3] = flecsEngineColorChannelToFloat(impl->clear_color.a);
}
