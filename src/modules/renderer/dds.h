#ifndef FLECS_ENGINE_DDS_H
#define FLECS_ENGINE_DDS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    uint32_t dxgi_format;     /* DXGI format from DX10 header */
    const uint8_t *pixel_data; /* pointer into loaded file data */
    uint32_t pixel_data_size;
} FlecsDdsInfo;

/* Parse a DDS file from memory. Returns false on error.
 * On success, info->pixel_data points into file_data (no copy). */
bool flecs_dds_parse(
    const uint8_t *file_data,
    uint32_t file_size,
    FlecsDdsInfo *info);

/* Returns the WGPUTextureFormat for a given DXGI format, or 0 if unsupported */
int flecs_dds_wgpu_format(
    uint32_t dxgi_format);

/* Returns the block size in bytes for a given DXGI format (16 for BC7) */
uint32_t flecs_dds_block_size(
    uint32_t dxgi_format);

#endif
