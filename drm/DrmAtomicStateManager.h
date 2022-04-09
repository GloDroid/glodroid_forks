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

#ifndef ANDROID_DRM_ATOMIC_STATE_MANAGER_H_
#define ANDROID_DRM_ATOMIC_STATE_MANAGER_H_

#include <pthread.h>

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <tuple>

#include "compositor/DrmKmsPlan.h"
#include "drm/DrmPlane.h"
#include "drm/ResourceManager.h"
#include "drm/VSyncWorker.h"
#include "drmhwcomposer.h"

namespace android {

struct AtomicCommitArgs {
  /* inputs. All fields are optional, but at least one has to be specified */
  bool test_only = false;
  std::optional<DrmMode> display_mode;
  std::optional<bool> active;
  std::shared_ptr<DrmKmsPlan> composition;

  /* out */
  UniqueFd out_fence;

  /* helpers */
  auto HasInputs() -> bool {
    return display_mode || active || composition;
  }
};

class DrmAtomicStateManager {
 public:
  explicit DrmAtomicStateManager(DrmDisplayPipeline *pipe) : pipe_(pipe){};
  DrmAtomicStateManager(const DrmAtomicStateManager &) = delete;
  ~DrmAtomicStateManager() = default;

  auto ExecuteAtomicCommit(AtomicCommitArgs &args) -> int;
  auto ActivateDisplayUsingDPMS() -> int;

 private:
  auto CommitFrame(AtomicCommitArgs &args) -> int;

  struct KmsState {
    /* Required to cleanup unused planes */
    std::vector<std::shared_ptr<BindingOwner<DrmPlane>>> used_planes;
    /* We have to hold a reference to framebuffer while displaying it ,
     * otherwise picture will blink */
    std::vector<std::shared_ptr<DrmFbIdHandle>> used_framebuffers;

    DrmModeUserPropertyBlobUnique mode_blob;

    /* To avoid setting the inactive state twice, which will fail the commit */
    bool crtc_active_state{};

    bool used;
  } active_frame_state_, staged_frame_state_;

  auto NewFrameState() -> KmsState {
    auto *prev_frame_state = staged_frame_state_.used ? &staged_frame_state_
                                                      : &active_frame_state_;
    return (KmsState){
        .used = true,
        .used_planes = prev_frame_state->used_planes,
        .used_framebuffers = prev_frame_state->used_framebuffers,
        .crtc_active_state = prev_frame_state->crtc_active_state,
    };
  }

  DrmDisplayPipeline *const pipe_;
};
}  // namespace android

#endif  // ANDROID_DRM_DISPLAY_COMPOSITOR_H_
