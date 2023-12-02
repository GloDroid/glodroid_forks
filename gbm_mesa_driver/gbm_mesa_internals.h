/*
 * Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

int gbm_mesa_driver_init(struct driver *drv);
void gbm_mesa_driver_close(struct driver *drv);

void gbm_mesa_resolve_format_and_use_flags(struct driver *drv, uint32_t format, uint64_t use_flags,
					   uint32_t *out_format, uint64_t *out_use_flags);

int gbm_mesa_bo_create2(struct bo *bo, uint32_t width, uint32_t height, uint32_t format,
		        uint64_t use_flags, bool test_only);

int gbm_mesa_bo_import(struct bo *bo, struct drv_import_fd_data *data);

int gbm_mesa_bo_destroy(struct bo *bo);

uint32_t gbm_mesa_bo_get_map_stride(struct bo *bo);
void *gbm_mesa_bo_map(struct bo *bo, struct vma *vma, uint32_t map_flags);
int gbm_mesa_bo_unmap(struct bo *bo, struct vma *vma);

int gbm_mesa_bo_get_plane_fd(struct bo *bo, size_t plane);
