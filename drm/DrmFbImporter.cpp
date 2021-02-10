/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "DrmFbImporter.h"

#include <hardware/gralloc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cinttypes>
#include <system_error>

#include "utils/log.h"
#include "utils/properties.h"

namespace android {

auto DrmFbIdHandle::CreateInstance(hwc_drm_bo_t *bo, GemHandle firstGemHandle,
                                   const std::shared_ptr<DrmDevice> &drm)
    -> std::shared_ptr<DrmFbIdHandle> {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): priv. constructor usage
  std::shared_ptr<DrmFbIdHandle> local(new DrmFbIdHandle(drm));

  local->GemHandles_[0] = firstGemHandle;
  int32_t err = 0;

  /* Framebuffer object creation require gem handle for every used plane */
  for (int i = 1; i < local->GemHandles_.size(); i++) {
    if (bo->prime_fds[i] > 0) {
      if (bo->prime_fds[i] != bo->prime_fds[0]) {
        err = drmPrimeFDToHandle(drm->fd(), bo->prime_fds[i],
                                 &local->GemHandles_.at(i));
        if (err != 0)
          ALOGE("failed to import prime fd %d errno=%d", bo->prime_fds[i],
                errno);
      } else {
        local->GemHandles_.at(i) = local->GemHandles_[0];
      }
    }
  }

  bool has_modifiers = bo->modifiers[0] != DRM_FORMAT_MOD_NONE &&
                       bo->modifiers[0] != DRM_FORMAT_MOD_INVALID;

  if (!drm->HasAddFb2ModifiersSupport() && has_modifiers) {
    ALOGE("No ADDFB2 with modifier support. Can't import modifier %" PRIu64,
          bo->modifiers[0]);
    local.reset();
    return local;
  }

  /* Create framebuffer object */
  if (!has_modifiers) {
    err = drmModeAddFB2(drm->fd(), bo->width, bo->height, bo->format,
                        &local->GemHandles_[0], &bo->pitches[0],
                        &bo->offsets[0], &local->FbId_, 0);
  } else {
    err = drmModeAddFB2WithModifiers(drm->fd(), bo->width, bo->height,
                                     bo->format, &local->GemHandles_[0],
                                     &bo->pitches[0], &bo->offsets[0],
                                     &bo->modifiers[0], &local->FbId_,
                                     DRM_MODE_FB_MODIFIERS);
  }
  if (err != 0) {
    ALOGE("could not create drm fb %d", err);
    local.reset();
  }

  return local;
}

DrmFbIdHandle::~DrmFbIdHandle() {
  /* Destroy framebuffer object */
  if (drmModeRmFB(Drm_->fd(), FbId_) != 0) {
    ALOGE("Failed to rm fb");
  }

  /* Close GEM handles.
   *
   * WARNING: TODO(nobody):
   * From Linux side libweston relies on libgbm to get KMS handle and never
   * closes it (handle is closed by libgbm on buffer destruction)
   * Probably we should offer similar approach to users (at least on user
   * request via system properties)
   */
  struct drm_gem_close gem_close {};
  for (unsigned int gem_handle : GemHandles_) {
    if (gem_handle == 0) {
      continue;
    }
    gem_close.handle = gem_handle;
    int32_t err = drmIoctl(Drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
    if (err != 0) {
      ALOGE("Failed to close gem handle %d, errno: %d", gem_handle, errno);
    }
  }
}

auto DrmFbImporter::GetOrCreateFbId(hwc_drm_bo_t *bo)
    -> std::shared_ptr<DrmFbIdHandle> {
  /* Lookup DrmFbIdHandle in cache first. First handle serves as a cache key. */
  GemHandle first_handle = 0;
  int32_t err = drmPrimeFDToHandle(Drm_->fd(), bo->prime_fds[0], &first_handle);

  if (err != 0) {
    ALOGE("Failed to import prime fd %d ret=%d", bo->prime_fds[0], err);
    return std::shared_ptr<DrmFbIdHandle>();
  }

  auto drm_fb_id_cached = DrmFbIdHandleCache_.find(first_handle);

  if (drm_fb_id_cached != DrmFbIdHandleCache_.end()) {
    if (auto drm_fb_id_handle_shared = drm_fb_id_cached->second.lock()) {
      return drm_fb_id_handle_shared;
    }
    DrmFbIdHandleCache_.erase(drm_fb_id_cached);
  }

  /* Cleanup cached empty weak pointers */
  const int cached_size_before_cleanup = 128;
  if (DrmFbIdHandleCache_.size() > cached_size_before_cleanup) {
    cleanupEmptyCacheElements();
  }

  /* No DrmFbIdHandle found in cache, create framebuffer object */
  auto fb_id_handle = DrmFbIdHandle::CreateInstance(bo, first_handle, Drm_);
  if (fb_id_handle) {
    DrmFbIdHandleCache_[first_handle] = fb_id_handle;
  }

  return fb_id_handle;
}

}  // namespace android
