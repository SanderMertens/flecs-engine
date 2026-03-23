#include "dds.h"
#include <string.h>

/* DDS magic number */
#define DDS_MAGIC 0x20534444 /* "DDS " */

/* DDS pixel format flags */
#define DDPF_FOURCC 0x4

/* DXGI formats we support */
#define DXGI_FORMAT_BC7_TYPELESS 97
#define DXGI_FORMAT_BC7_UNORM    98
#define DXGI_FORMAT_BC7_UNORM_SRGB 99
#define DXGI_FORMAT_BC1_TYPELESS 70
#define DXGI_FORMAT_BC1_UNORM    71
#define DXGI_FORMAT_BC1_UNORM_SRGB 72
#define DXGI_FORMAT_BC3_TYPELESS 76
#define DXGI_FORMAT_BC3_UNORM    77
#define DXGI_FORMAT_BC3_UNORM_SRGB 78

static uint32_t flecs_dds_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool flecs_dds_parse(
    const uint8_t *file_data,
    uint32_t file_size,
    FlecsDdsInfo *info)
{
    memset(info, 0, sizeof(FlecsDdsInfo));

    /* Need at least magic(4) + header(124) */
    if (file_size < 128) {
        return false;
    }

    if (flecs_dds_read_u32(file_data) != DDS_MAGIC) {
        return false;
    }

    const uint8_t *hdr = file_data + 4;
    info->height = flecs_dds_read_u32(hdr + 8);
    info->width = flecs_dds_read_u32(hdr + 12);
    info->mip_count = flecs_dds_read_u32(hdr + 24);
    if (info->mip_count == 0) {
        info->mip_count = 1;
    }

    /* Check pixel format for DX10 extended header */
    uint32_t pf_flags = flecs_dds_read_u32(hdr + 76);
    const uint8_t *fourcc = hdr + 80;

    uint32_t header_size = 128; /* magic + standard header */

    if ((pf_flags & DDPF_FOURCC) &&
        fourcc[0] == 'D' && fourcc[1] == 'X' &&
        fourcc[2] == '1' && fourcc[3] == '0')
    {
        /* DX10 extended header: 20 bytes */
        if (file_size < 148) {
            return false;
        }
        const uint8_t *dx10 = file_data + 128;
        info->dxgi_format = flecs_dds_read_u32(dx10);
        header_size = 148;
    } else {
        /* Non-DX10 DDS not supported */
        return false;
    }

    if (header_size >= file_size) {
        return false;
    }

    info->pixel_data = file_data + header_size;
    info->pixel_data_size = file_size - header_size;

    return true;
}

int flecs_dds_wgpu_format(
    uint32_t dxgi_format)
{
    switch (dxgi_format) {
    /* BC1 (DXT1) - 8 bytes per 4x4 block */
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
        return 0x0000002C; /* WGPUTextureFormat_BC1RGBAUnorm */
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return 0x0000002D; /* WGPUTextureFormat_BC1RGBAUnormSrgb */

    /* BC3 (DXT5) - 16 bytes per 4x4 block */
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
        return 0x00000030; /* WGPUTextureFormat_BC3RGBAUnorm */
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return 0x00000031; /* WGPUTextureFormat_BC3RGBAUnormSrgb */

    /* BC7 - 16 bytes per 4x4 block */
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
        return 0x00000038; /* WGPUTextureFormat_BC7RGBAUnorm */
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 0x00000039; /* WGPUTextureFormat_BC7RGBAUnormSrgb */

    default:
        return 0;
    }
}

uint32_t flecs_dds_block_size(
    uint32_t dxgi_format)
{
    switch (dxgi_format) {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return 8;
    default:
        return 16;
    }
}
