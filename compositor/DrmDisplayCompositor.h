/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ANDROID_DRM_DISPLAY_COMPOSITOR_H_
#define ANDROID_DRM_DISPLAY_COMPOSITOR_H_

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <pthread.h>

#include <functional>
#include <memory>
#include <sstream>
#include <tuple>

#include "DrmDisplayComposition.h"
#include "Planner.h"
#include "drm/ResourceManager.h"
#include "drm/VSyncWorker.h"
#include "drmhwcomposer.h"

namespace android {

class DrmDisplayCompositor {
 public:
  DrmDisplayCompositor();
  ~DrmDisplayCompositor();

  auto Init(ResourceManager *resource_manager, int display) -> int;

  std::unique_ptr<DrmDisplayComposition> CreateInitializedComposition() const;
  int ApplyComposition(std::unique_ptr<DrmDisplayComposition> composition);
  int TestComposition(DrmDisplayComposition *composition);
  int Composite();
  void ClearDisplay();
  UniqueFd TakeOutFence() {
    if (!active_composition_) {
      return UniqueFd();
    }
    return std::move(active_composition_->out_fence_);
  }

  std::tuple<uint32_t, uint32_t, int> GetActiveModeResolution();

 private:
  struct ModeState {
    DrmMode mode;
    DrmModeUserPropertyBlobUnique blob;
    DrmModeUserPropertyBlobUnique old_blob;
  };

  DrmDisplayCompositor(const DrmDisplayCompositor &) = delete;

  int CommitFrame(DrmDisplayComposition *display_comp, bool test_only);
  int ApplyDpms(DrmDisplayComposition *display_comp);
  int DisablePlanes(DrmDisplayComposition *display_comp);

  void ApplyFrame(std::unique_ptr<DrmDisplayComposition> composition,
                  int status);

  auto CreateModeBlob(const DrmMode &mode) -> DrmModeUserPropertyBlobUnique;

  ResourceManager *resource_manager_;
  int display_;

  std::unique_ptr<DrmDisplayComposition> active_composition_;

  bool initialized_;
  bool active_;
  bool use_hw_overlays_;

  ModeState mode_;

  std::unique_ptr<Planner> planner_;
};
}  // namespace android

#endif  // ANDROID_DRM_DISPLAY_COMPOSITOR_H_
