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

struct AtomicCommitArgs {
  /* inputs. All fields are optional, but at least one has to be specified */
  bool test_only = false;
  std::optional<DrmMode> display_mode;
  std::optional<bool> active;
  std::shared_ptr<DrmDisplayComposition> composition;
  /* 'clear' should never be used together with 'composition' */
  bool clear_active_composition = false;

  /* out */
  UniqueFd out_fence;

  /* helpers */
  auto HasInputs() -> bool {
    return display_mode || active || composition || clear_active_composition;
  }
};

class DrmDisplayCompositor {
 public:
  DrmDisplayCompositor() = default;
  ~DrmDisplayCompositor() = default;
  auto Init(ResourceManager *resource_manager, int display) -> int;

  std::unique_ptr<DrmDisplayComposition> CreateInitializedComposition() const;

  auto ExecuteAtomicCommit(AtomicCommitArgs &args) -> int;

 private:
  DrmDisplayCompositor(const DrmDisplayCompositor &) = delete;

  auto CommitFrame(AtomicCommitArgs &args) -> int;

  struct {
    std::shared_ptr<DrmDisplayComposition> composition;
    DrmModeUserPropertyBlobUnique mode_blob;
    bool active_state{};
  } active_kms_data;

  ResourceManager *resource_manager_ = nullptr;
  std::unique_ptr<Planner> planner_;
  bool initialized_{};
  int display_ = -1;
};
}  // namespace android

#endif  // ANDROID_DRM_DISPLAY_COMPOSITOR_H_
