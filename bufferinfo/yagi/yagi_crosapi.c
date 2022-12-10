/*
 * Copyright (C) 2018-2022 The Android Open Source Project
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

/* drm_hwc buffer information external supplier that wraps
 * minigbm gralloc0 perform API.
 */

#define LOG_TAG "yagi_minigbm_crosapi"

#include <dlfcn.h>
#include <errno.h>
#include <hardware/gralloc.h>
#include <log/log.h>
#include <string.h>

#include "android_yagi.h"

#define EXPORT __attribute__((visibility("default")))

struct yagi {
  const gralloc_module_t *gralloc;
  int refcount;
};

struct yagi cros_gralloc = {0};

/* ref: Minigbm/cros_gralloc/gralloc0/gralloc0.cc:39 */
const int kCrosGrallocDrmGetFormat = 1;
const int kCrosGrallocDrmGetDimensions = 2;
const int kCrosGrallocDrmGetBufferInfo = 4;
const int kCrosGrallocDrmGetUsage = 5;

/* ref: Minigbm/cros_gralloc/gralloc0/gralloc0.cc:23 */
struct CrosGralloc0BufferInfo {
  uint32_t drm_fourcc;
  int num_fds;
  int fds[4];
  uint64_t modifier;
  int offset[4];
  int stride[4];
};

EXPORT int yagi_bi_get(struct yagi *yagi, buffer_handle_t handle,
                       struct yagi_bi_v1 *out_buf_info, int version, int size) {
  struct CrosGralloc0BufferInfo info = {0};
  int ret = 0;
  uint32_t usage = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  int32_t droid_format = 0;

  if (yagi != &cros_gralloc || handle == NULL || out_buf_info == NULL ||
      version != 1 || size != sizeof(struct yagi_bi_v1)) {
    ALOGE("Invalid arguments");
    return -EINVAL;
  }

  ret = yagi->gralloc->perform(yagi->gralloc, kCrosGrallocDrmGetDimensions,
                               handle, &width, &height);
  if (ret != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_DIMENSIONS operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return ret;
  }

  ret = yagi->gralloc->perform(yagi->gralloc, kCrosGrallocDrmGetFormat, handle,
                               &droid_format);
  if (ret != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_FORMAT operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return ret;
  }

  ret = yagi->gralloc->perform(yagi->gralloc, kCrosGrallocDrmGetUsage, handle,
                               &usage);
  if (ret != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_USAGE operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return ret;
  }

  ret = yagi->gralloc->perform(yagi->gralloc, kCrosGrallocDrmGetBufferInfo,
                               handle, &info);
  if (ret != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_BUFFER_INFO operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return ret;
  }

  out_buf_info->width = width;
  out_buf_info->height = height;

  out_buf_info->drm_format = info.drm_fourcc;

  for (int i = 0; i < info.num_fds; i++) {
    out_buf_info->modifiers[i] = info.modifier;
    out_buf_info->prime_fds[i] = info.fds[i];
    out_buf_info->pitches[i] = info.stride[i];
    out_buf_info->offsets[i] = info.offset[i];
  }

  out_buf_info->num_planes = info.num_fds;
  out_buf_info->yagi_flags = 0;

  return 0;
}

const char kCrosGrallocModuleName[] = "CrOS Gralloc";

EXPORT struct yagi *yagi_init(int *out_yagi_api_version) {
  int ret = 0;

  if (cros_gralloc.refcount == 0) {
    ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                        (const hw_module_t **)&cros_gralloc.gralloc);

    if (ret != 0) {
      ALOGE("Failed to get gralloc hwmodule");
      return NULL;
    }

    ret = strcmp(cros_gralloc.gralloc->common.name, kCrosGrallocModuleName);
    if (ret != 0) {
      ALOGE("Gralloc name isn't valid: Expected: \"%s\", Actual: \"%s\"",
            kCrosGrallocModuleName, cros_gralloc.gralloc->common.name);
      dlclose(cros_gralloc.gralloc->common.dso);
      return NULL;
    }

    if (cros_gralloc.gralloc->perform == NULL) {
      ALOGE(
          "CrOS gralloc has no perform call implemented. Please upgrade "
          "minigbm.");
      dlclose(cros_gralloc.gralloc->common.dso);
      return NULL;
    }
  }

  cros_gralloc.refcount++;
  *out_yagi_api_version = 1;
  return &cros_gralloc;
}

EXPORT void yagi_destroy(struct yagi *yagi) {
  if (yagi != &cros_gralloc || cros_gralloc.refcount == 0) {
    ALOGE("%s: Invalid arguments", __func__);
    return;
  }

  if (cros_gralloc.refcount == 0) {
    ALOGE("%s: Invalid state", __func__);
    return;
  }

  cros_gralloc.refcount--;
  if (cros_gralloc.refcount == 0) {
    dlclose(cros_gralloc.gralloc->common.dso);
  }
}
