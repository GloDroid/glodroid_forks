#pragma once

#define LOG_TAG "MESAGBM-GRALLOC"

#include <cutils/properties.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <glob.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int dmabuf_driver_init(struct driver *drv);
void dmabuf_driver_close(struct driver *drv);

void dmabuf_resolve_format_and_use_flags(struct driver *drv, uint32_t format, uint64_t use_flags,
					 uint32_t *out_format, uint64_t *out_use_flags);

int dmabuf_bo_create(struct bo *bo, uint32_t width, uint32_t height, uint32_t format,
		     uint64_t use_flags);

int dmabuf_bo_import(struct bo *bo, struct drv_import_fd_data *data);

int dmabuf_bo_destroy(struct bo *bo);

void *dmabuf_bo_map(struct bo *bo, struct vma *vma, size_t plane, uint32_t map_flags);
int dmabuf_bo_unmap(struct bo *bo, struct vma *vma);
int dmabuf_bo_flush(struct bo *bo, struct mapping *mapping);

int dmabuf_bo_get_plane_fd(struct bo *bo, size_t plane);
