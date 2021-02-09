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

#ifndef DRM_DRMFBIMPORTER_H_
#define DRM_DRMFBIMPORTER_H_

#include <drm/drm_fourcc.h>
#include <hardware/gralloc.h>

#include <array>
#include <map>

#include "drm/DrmDevice.h"
#include "drmhwcgralloc.h"

#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID 0
#endif

using GemHandle = uint32_t;

namespace android {

class DrmFbIdHandle {
 public:
  static auto CreateInstance(hwc_drm_bo_t *bo, GemHandle firstGemHandle,
                             const std::shared_ptr<DrmDevice> &drm)
      -> std::shared_ptr<DrmFbIdHandle>;

  ~DrmFbIdHandle();
  DrmFbIdHandle(DrmFbIdHandle &&) = delete;
  DrmFbIdHandle(const DrmFbIdHandle &) = delete;
  auto operator=(const DrmFbIdHandle &) = delete;
  auto operator=(DrmFbIdHandle &&) = delete;

  auto GetFbId [[nodiscard]] () const -> uint32_t {
    return FbId_;
  }

 private:
  explicit DrmFbIdHandle(std::shared_ptr<DrmDevice> drm)
      : Drm_(std::move(drm)){};

  const std::shared_ptr<DrmDevice> Drm_;

  uint32_t FbId_{};
  std::array<GemHandle, HWC_DRM_BO_MAX_PLANES> GemHandles_{};
};

class DrmFbImporter {
 public:
  explicit DrmFbImporter(std::shared_ptr<DrmDevice> drm)
      : Drm_(std::move(drm)){};
  ~DrmFbImporter() = default;
  DrmFbImporter(const DrmFbImporter &) = delete;
  DrmFbImporter(DrmFbImporter &&) = delete;
  auto operator=(const DrmFbImporter &) = delete;
  auto operator=(DrmFbImporter &&) = delete;

  auto GetOrCreateFbId(hwc_drm_bo_t *bo) -> std::shared_ptr<DrmFbIdHandle>;

 private:
  void cleanupEmptyCacheElements() {
    for (auto it = DrmFbIdHandleCache_.begin();
         it != DrmFbIdHandleCache_.end();) {
      if (it->second.expired()) {
        it = DrmFbIdHandleCache_.erase(it);
      } else {
        ++it;
      }
    }
  }

  const std::shared_ptr<DrmDevice> Drm_;

  std::map<GemHandle, std::weak_ptr<DrmFbIdHandle>> DrmFbIdHandleCache_;
};

}  // namespace android

#endif
