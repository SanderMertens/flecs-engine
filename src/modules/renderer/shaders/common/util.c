#include "../../renderer.h"

bool flecsEngine_shader_usesIbl(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@group(1) @binding(0) var ibl_prefiltered_env") != NULL;
}

bool flecsEngine_shader_usesShadow(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@group(2) @binding(0) var shadow_map") != NULL;
}

bool flecsEngine_shader_usesCluster(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@group(3) @binding(0) var<uniform> cluster_info") != NULL;
}
