/*
 * Copyright (C) 2020 The Android Open Source Project
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

#define LOG_TAG "hwc-platform-drm-generic"

#include "DrmGenericImporter.h"

#include <cutils/properties.h>
#include <gralloc_handle.h>
#include <hardware/gralloc.h>
#include <log/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cinttypes>

namespace android {

DrmGenericImporter::DrmGenericImporter(DrmDevice *drm) : drm_(drm) {
  uint64_t cap_value = 0;
  if (drmGetCap(drm_->fd(), DRM_CAP_ADDFB2_MODIFIERS, &cap_value)) {
    ALOGE("drmGetCap failed. Fallback to no modifier support.");
    cap_value = 0;
  }
  has_modifier_support_ = cap_value != 0;
}

int DrmGenericImporter::ImportBuffer(hwc_drm_bo_t *bo) {
  int ret = 0;

  for (int i = 0; i < HWC_DRM_BO_MAX_PLANES; i++) {
    if (bo->prime_fds[i] > 0) {
      if (i == 0 || bo->prime_fds[i] != bo->prime_fds[0]) {
        ret = drmPrimeFDToHandle(drm_->fd(), bo->prime_fds[i],
                                 &bo->gem_handles[i]);
        if (ret) {
          ALOGE("failed to import prime fd %d ret=%d", bo->prime_fds[i], ret);
          return ret;
        }
      } else {
        bo->gem_handles[i] = bo->gem_handles[0];
      }
    }
  }

  bool has_modifiers = bo->modifiers[0] != DRM_FORMAT_MOD_NONE &&
                       bo->modifiers[0] != DRM_FORMAT_MOD_INVALID;

  if (!has_modifier_support_ && has_modifiers) {
    ALOGE("No ADDFB2 with modifier support. Can't import modifier %" PRIu64,
          bo->modifiers[0]);
    return -EINVAL;
  }

  if (!has_modifiers)
    ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                        bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id,
                        0);
  else
    ret = drmModeAddFB2WithModifiers(drm_->fd(), bo->width, bo->height,
                                     bo->format, bo->gem_handles, bo->pitches,
                                     bo->offsets, bo->modifiers, &bo->fb_id,
                                     DRM_MODE_FB_MODIFIERS);

  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    return ret;
  }

  for (unsigned int gem_handle : bo->gem_handles) {
    if (!gem_handle)
      continue;

    ImportHandle(gem_handle);
  }

  return ret;
}

int DrmGenericImporter::ReleaseBuffer(hwc_drm_bo_t *bo) {
  if (bo->fb_id)
    if (drmModeRmFB(drm_->fd(), bo->fb_id))
      ALOGE("Failed to rm fb");

  for (unsigned int &gem_handle : bo->gem_handles) {
    if (!gem_handle)
      continue;

    if (ReleaseHandle(gem_handle))
      ALOGE("Failed to release gem handle %d", gem_handle);
    else
      gem_handle = 0;
  }
  return 0;
}

int DrmGenericImporter::ImportHandle(uint32_t gem_handle) {
  gem_refcount_[gem_handle]++;

  return 0;
}

int DrmGenericImporter::ReleaseHandle(uint32_t gem_handle) {
  if (--gem_refcount_[gem_handle])
    return 0;

  gem_refcount_.erase(gem_handle);

  return CloseHandle(gem_handle);
}

int DrmGenericImporter::CloseHandle(uint32_t gem_handle) {
  struct drm_gem_close gem_close {};
  gem_close.handle = gem_handle;
  int ret = drmIoctl(drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
  if (ret)
    ALOGE("Failed to close gem handle %d %d", gem_handle, ret);

  return ret;
}
}  // namespace android
