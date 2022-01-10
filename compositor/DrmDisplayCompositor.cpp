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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#define LOG_TAG "hwc-drm-display-compositor"

#include "DrmDisplayCompositor.h"

#include <drm/drm_mode.h>
#include <pthread.h>
#include <sched.h>
#include <sync/sync.h>
#include <utils/Trace.h>

#include <array>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#include "drm/DrmCrtc.h"
#include "drm/DrmDevice.h"
#include "drm/DrmPlane.h"
#include "drm/DrmUnique.h"
#include "utils/autolock.h"
#include "utils/log.h"

namespace android {

auto DrmDisplayCompositor::Init(ResourceManager *resource_manager, int display)
    -> int {
  resource_manager_ = resource_manager;
  display_ = display;
  DrmDevice *drm = resource_manager_->GetDrmDevice(display);
  if (!drm) {
    ALOGE("Could not find drmdevice for display");
    return -EINVAL;
  }

  initialized_ = true;
  return 0;
}

std::unique_ptr<DrmDisplayComposition>
DrmDisplayCompositor::CreateInitializedComposition() const {
  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  DrmCrtc *crtc = drm->GetCrtcForDisplay(display_);
  if (!crtc) {
    ALOGE("Failed to find crtc for display = %d", display_);
    return {};
  }

  return std::make_unique<DrmDisplayComposition>(crtc);
}

// NOLINTNEXTLINE (readability-function-cognitive-complexity): Fixme
auto DrmDisplayCompositor::CommitFrame(AtomicCommitArgs &args) -> int {
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

  if (args.clear_active_composition && args.composition) {
    ALOGE("%s: Invalid arguments", __func__);
    return -EINVAL;
  }

  auto new_frame_state = NewFrameState();

  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);

  DrmConnector *connector = drm->GetConnectorForDisplay(display_);
  if (!connector) {
    ALOGE("Could not locate connector for display %d", display_);
    return -ENODEV;
  }
  DrmCrtc *crtc = drm->GetCrtcForDisplay(display_);
  if (!crtc) {
    ALOGE("Could not locate crtc for display %d", display_);
    return -ENODEV;
  }

  auto pset = MakeDrmModeAtomicReqUnique();
  if (!pset) {
    ALOGE("Failed to allocate property set");
    return -ENOMEM;
  }

  int64_t out_fence = -1;
  if (crtc->out_fence_ptr_property() &&
      !crtc->out_fence_ptr_property().AtomicSet(*pset, (uint64_t)&out_fence)) {
    return -EINVAL;
  }

  if (args.active) {
    new_frame_state.crtc_active_state = *args.active;
    if (!crtc->active_property().AtomicSet(*pset, *args.active) ||
        !connector->crtc_id_property().AtomicSet(*pset, crtc->id())) {
      return -EINVAL;
    }
  }

  if (args.display_mode) {
    new_frame_state.mode_blob = args.display_mode.value().CreateModeBlob(
        *resource_manager_->GetDrmDevice(display_));

    if (!new_frame_state.mode_blob) {
      ALOGE("Failed to create mode_blob");
      return -EINVAL;
    }

    if (!crtc->mode_property().AtomicSet(*pset, *new_frame_state.mode_blob)) {
      return -EINVAL;
    }
  }

  auto unused_planes = new_frame_state.used_planes;

  if (args.composition) {
    new_frame_state.used_framebuffers.clear();
    new_frame_state.used_planes.clear();

    std::vector<DrmHwcLayer> &layers = args.composition->layers();
    std::vector<DrmCompositionPlane> &comp_planes = args.composition
                                                        ->composition_planes();

    for (DrmCompositionPlane &comp_plane : comp_planes) {
      DrmPlane *plane = comp_plane.plane();
      size_t source_layer = comp_plane.source_layer();

      if (source_layer >= layers.size()) {
        ALOGE("Source layer index %zu out of bounds %zu", source_layer,
              layers.size());
        return -EINVAL;
      }
      DrmHwcLayer &layer = layers[source_layer];

      new_frame_state.used_framebuffers.emplace_back(layer.fb_id_handle);
      new_frame_state.used_planes.emplace_back(plane);

      /* Remove from 'unused' list, since plane is re-used */
      auto &v = unused_planes;
      v.erase(std::remove(v.begin(), v.end(), plane), v.end());

      if (plane->AtomicSetState(*pset, layer, source_layer, crtc->id()) != 0) {
        return -EINVAL;
      }
    }
  }

  if (args.clear_active_composition) {
    new_frame_state.used_framebuffers.clear();
    new_frame_state.used_planes.clear();
  }

  if (args.clear_active_composition || args.composition) {
    for (auto *plane : unused_planes) {
      if (plane->AtomicDisablePlane(*pset) != 0) {
        return -EINVAL;
      }
    }
  }

  uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
  if (args.test_only)
    flags |= DRM_MODE_ATOMIC_TEST_ONLY;

  int err = drmModeAtomicCommit(drm->fd(), pset.get(), flags, drm);
  if (err) {
    if (!args.test_only)
      ALOGE("Failed to commit pset ret=%d\n", err);
    return err;
  }

  if (!args.test_only) {
    if (args.display_mode) {
      /* TODO(nobody): we still need this for synthetic vsync, remove after
       * vsync reworked */
      connector->set_active_mode(*args.display_mode);
    }

    active_frame_state_ = std::move(new_frame_state);

    if (crtc->out_fence_ptr_property()) {
      args.out_fence = UniqueFd((int)out_fence);
    }
  }

  return 0;
}

auto DrmDisplayCompositor::ExecuteAtomicCommit(AtomicCommitArgs &args) -> int {
  int err = CommitFrame(args);

  if (!args.test_only) {
    if (err) {
      ALOGE("Composite failed for display %d", display_);
      // Disable the hw used by the last active composition. This allows us to
      // signal the release fences from that composition to avoid hanging.
      AtomicCommitArgs cl_args = {.clear_active_composition = true};
      if (CommitFrame(cl_args)) {
        ALOGE("Failed to clean-up active composition for display %d", display_);
      }
      return err;
    }
  }

  return err;
}  // namespace android

auto DrmDisplayCompositor::ActivateDisplayUsingDPMS() -> int {
  auto *drm = resource_manager_->GetDrmDevice(display_);
  auto *connector = drm->GetConnectorForDisplay(display_);
  if (connector == nullptr) {
    ALOGE("Could not locate connector for display %d", display_);
    return -ENODEV;
  }

  if (connector->dpms_property()) {
    drmModeConnectorSetProperty(drm->fd(), connector->id(),
                                connector->dpms_property().id(),
                                DRM_MODE_DPMS_ON);
  }
  return 0;
}

}  // namespace android
