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

#undef NDEBUG /* Required for assert to work */

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#define LOG_TAG "hwc-drm-atomic-state-manager"

#include "DrmAtomicStateManager.h"

#include <drm/drm_mode.h>
#include <pthread.h>
#include <sched.h>
#include <sync/sync.h>
#include <utils/Trace.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#include "drm/DrmCrtc.h"
#include "drm/DrmDevice.h"
#include "drm/DrmPlane.h"
#include "drm/DrmUnique.h"
#include "utils/log.h"

namespace android {

// NOLINTNEXTLINE (readability-function-cognitive-complexity): Fixme
auto DrmAtomicStateManager::CommitFrame(AtomicCommitArgs &args) -> int {
  // NOLINTNEXTLINE(misc-const-correctness)
  ATRACE_CALL();

  if (args.active && *args.active == active_frame_state_.crtc_active_state) {
    /* Don't set the same state twice */
    args.active.reset();
  }

  if (!args.HasInputs()) {
    /* nothing to do */
    return 0;
  }

  if (!active_frame_state_.crtc_active_state) {
    /* Force activate display */
    args.active = true;
  }

  auto new_frame_state = NewFrameState();

  auto *drm = pipe_->device;
  auto *connector = pipe_->connector->Get();
  auto *crtc = pipe_->crtc->Get();

  auto pset = MakeDrmModeAtomicReqUnique();
  if (!pset) {
    ALOGE("Failed to allocate property set");
    return -ENOMEM;
  }

  int out_fence = -1;
  if (!crtc->GetOutFencePtrProperty().AtomicSet(*pset, uint64_t(&out_fence))) {
    return -EINVAL;
  }

  bool nonblock = true;

  if (args.active) {
    nonblock = false;
    new_frame_state.crtc_active_state = *args.active;
    if (!crtc->GetActiveProperty().AtomicSet(*pset, *args.active ? 1 : 0) ||
        !connector->GetCrtcIdProperty().AtomicSet(*pset, crtc->GetId())) {
      return -EINVAL;
    }
  }

  if (args.display_mode) {
    new_frame_state.mode_blob = args.display_mode.value().CreateModeBlob(*drm);

    if (!new_frame_state.mode_blob) {
      ALOGE("Failed to create mode_blob");
      return -EINVAL;
    }

    if (!crtc->GetModeProperty().AtomicSet(*pset, *new_frame_state.mode_blob)) {
      return -EINVAL;
    }
  }

  auto unused_planes = new_frame_state.used_planes;

  if (args.composition) {
    new_frame_state.used_planes.clear();

    for (auto &joining : args.composition->plan) {
      DrmPlane *plane = joining.plane->Get();
      LayerData &layer = joining.layer;

      new_frame_state.used_framebuffers.emplace_back(layer.fb);
      new_frame_state.used_planes.emplace_back(joining.plane);

      /* Remove from 'unused' list, since plane is re-used */
      auto &v = unused_planes;
      v.erase(std::remove(v.begin(), v.end(), joining.plane), v.end());

      if (plane->AtomicSetState(*pset, layer, joining.z_pos, crtc->GetId()) !=
          0) {
        return -EINVAL;
      }
    }
  }

  if (args.composition) {
    for (auto &plane : unused_planes) {
      if (plane->Get()->AtomicDisablePlane(*pset) != 0) {
        return -EINVAL;
      }
    }
  }

  uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

  if (args.test_only) {
    return drmModeAtomicCommit(drm->GetFd(), pset.get(),
                               flags | DRM_MODE_ATOMIC_TEST_ONLY, drm);
  }

  if (last_present_fence_) {
    // NOLINTNEXTLINE(misc-const-correctness)
    ATRACE_NAME("WaitPriorFramePresented");

    constexpr int kTimeoutMs = 500;
    const int err = sync_wait(last_present_fence_.Get(), kTimeoutMs);
    if (err != 0) {
      ALOGE("sync_wait(fd=%i) returned: %i (errno: %i)",
            last_present_fence_.Get(), err, errno);
    }

    CleanupPriorFrameResources();
  }

  if (nonblock) {
    flags |= DRM_MODE_ATOMIC_NONBLOCK;
  }

  auto err = drmModeAtomicCommit(drm->GetFd(), pset.get(), flags, drm);

  if (err != 0) {
    ALOGE("Failed to commit pset ret=%d\n", err);
    return err;
  }

  if (nonblock) {
    last_present_fence_ = UniqueFd::Dup(out_fence);
    staged_frame_state_ = std::move(new_frame_state);
    frames_staged_++;
    ptt_->Notify();
  } else {
    active_frame_state_ = std::move(new_frame_state);
  }

  if (args.display_mode) {
    /* TODO(nobody): we still need this for synthetic vsync, remove after
     * vsync reworked */
    connector->SetActiveMode(*args.display_mode);
  }

  args.out_fence = UniqueFd(out_fence);

  return 0;
}

PresentTrackerThread::PresentTrackerThread(DrmAtomicStateManager *st_man)
    : st_man_(st_man),
      mutex_(&st_man_->pipe_->device->GetResMan().GetMainLock()) {
  pt_ = std::thread(&PresentTrackerThread::PresentTrackerThreadFn, this);
}

PresentTrackerThread::~PresentTrackerThread() {
  ALOGI("PresentTrackerThread successfully destroyed");
}

void PresentTrackerThread::PresentTrackerThreadFn() {
  /* object should be destroyed on thread exit */
  auto self = std::unique_ptr<PresentTrackerThread>(this);

  int tracking_at_the_moment = -1;

  for (;;) {
    UniqueFd present_fence;

    {
      std::unique_lock lk(*mutex_);
      cv_.wait(lk, [&] {
        return st_man_ == nullptr ||
               st_man_->frames_staged_ > tracking_at_the_moment;
      });

      if (st_man_ == nullptr) {
        break;
      }

      tracking_at_the_moment = st_man_->frames_staged_;

      present_fence = UniqueFd::Dup(st_man_->last_present_fence_.Get());
      if (!present_fence) {
        continue;
      }
    }

    {
      // NOLINTNEXTLINE(misc-const-correctness)
      ATRACE_NAME("AsyncWaitForBuffersSwap");
      constexpr int kTimeoutMs = 500;
      auto err = sync_wait(present_fence.Get(), kTimeoutMs);
      if (err != 0) {
        ALOGE("sync_wait(fd=%i) returned: %i (errno: %i)", present_fence.Get(),
              err, errno);
      }
    }

    {
      const std::unique_lock lk(*mutex_);
      if (st_man_ == nullptr) {
        break;
      }

      /* If resources is already cleaned-up by main thread, skip */
      if (tracking_at_the_moment > st_man_->frames_tracked_) {
        st_man_->CleanupPriorFrameResources();
      }
    }
  }
}

void DrmAtomicStateManager::CleanupPriorFrameResources() {
  assert(frames_staged_ - frames_tracked_ == 1);
  assert(last_present_fence_);

  // NOLINTNEXTLINE(misc-const-correctness)
  ATRACE_NAME("CleanupPriorFrameResources");
  frames_tracked_++;
  active_frame_state_ = std::move(staged_frame_state_);
  last_present_fence_ = {};
}

auto DrmAtomicStateManager::ExecuteAtomicCommit(AtomicCommitArgs &args) -> int {
  auto err = CommitFrame(args);

  if (!args.test_only) {
    if (err != 0) {
      ALOGE("Composite failed for pipeline %s",
            pipe_->connector->Get()->GetName().c_str());
      // Disable the hw used by the last active composition. This allows us to
      // signal the release fences from that composition to avoid hanging.
      AtomicCommitArgs cl_args{};
      cl_args.composition = std::make_shared<DrmKmsPlan>();
      if (CommitFrame(cl_args) != 0) {
        ALOGE("Failed to clean-up active composition for pipeline %s",
              pipe_->connector->Get()->GetName().c_str());
      }
      return err;
    }
  }

  return err;
}  // namespace android

auto DrmAtomicStateManager::ActivateDisplayUsingDPMS() -> int {
  return drmModeConnectorSetProperty(pipe_->device->GetFd(),
                                     pipe_->connector->Get()->GetId(),
                                     pipe_->connector->Get()
                                         ->GetDpmsProperty()
                                         .id(),
                                     DRM_MODE_DPMS_ON);
}

}  // namespace android
