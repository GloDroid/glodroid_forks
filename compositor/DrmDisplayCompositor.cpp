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
  planner_ = Planner::CreateInstance(drm);

  initialized_ = true;
  return 0;
}

std::unique_ptr<DrmDisplayComposition>
DrmDisplayCompositor::CreateInitializedComposition() const {
  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  DrmCrtc *crtc = drm->GetCrtcForDisplay(display_);
  if (!crtc) {
    ALOGE("Failed to find crtc for display = %d", display_);
    return std::unique_ptr<DrmDisplayComposition>();
  }

  return std::make_unique<DrmDisplayComposition>(crtc, planner_.get());
}

auto DrmDisplayCompositor::CommitFrame(AtomicCommitArgs &args) -> int {
  ATRACE_CALL();

  if (args.active && *args.active == active_kms_data.active_state) {
    /* Don't set the same state twice */
    args.active.reset();
  }

  if (!args.HasInputs()) {
    /* nothing to do */
    return 0;
  }

  if (!active_kms_data.active_state) {
    /* Force activate display */
    args.active = true;
  }

  if (args.clear_active_composition && args.composition) {
    ALOGE("%s: Invalid arguments", __func__);
    return -EINVAL;
  }

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

  DrmModeUserPropertyBlobUnique mode_blob;

  if (args.active) {
    if (!crtc->active_property().AtomicSet(*pset, *args.active) ||
        !connector->crtc_id_property().AtomicSet(*pset, crtc->id())) {
      return -EINVAL;
    }
  }

  if (args.display_mode) {
    mode_blob = args.display_mode.value().CreateModeBlob(
        *resource_manager_->GetDrmDevice(display_));

    if (!mode_blob) {
      ALOGE("Failed to create mode_blob");
      return -EINVAL;
    }

    if (!crtc->mode_property().AtomicSet(*pset, *mode_blob)) {
      return -EINVAL;
    }
  }

  if (args.composition) {
    std::vector<DrmHwcLayer> &layers = args.composition->layers();
    std::vector<DrmCompositionPlane> &comp_planes = args.composition
                                                        ->composition_planes();

    for (DrmCompositionPlane &comp_plane : comp_planes) {
      DrmPlane *plane = comp_plane.plane();
      std::vector<size_t> &source_layers = comp_plane.source_layers();

      if (comp_plane.type() != DrmCompositionPlane::Type::kDisable) {
        if (source_layers.size() > 1) {
          ALOGE("Can't handle more than one source layer sz=%zu type=%d",
                source_layers.size(), comp_plane.type());
          continue;
        }

        if (source_layers.empty() || source_layers.front() >= layers.size()) {
          ALOGE("Source layer index %zu out of bounds %zu type=%d",
                source_layers.front(), layers.size(), comp_plane.type());
          return -EINVAL;
        }
        DrmHwcLayer &layer = layers[source_layers.front()];

        if (plane->AtomicSetState(*pset, layer, source_layers.front(),
                                  crtc->id()) != 0) {
          return -EINVAL;
        }
      } else {
        if (plane->AtomicDisablePlane(*pset) != 0) {
          return -EINVAL;
        }
      }
    }
  }

  if (args.clear_active_composition && active_kms_data.composition) {
    auto &comp_planes = active_kms_data.composition->composition_planes();
    for (auto &comp_plane : comp_planes) {
      if (comp_plane.plane()->AtomicDisablePlane(*pset) != 0) {
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
      connector->set_active_mode(*args.display_mode);
      active_kms_data.mode_blob = std::move(mode_blob);
    }

    if (args.clear_active_composition) {
      active_kms_data.composition.reset();
    }

    if (args.composition) {
      active_kms_data.composition = args.composition;
    }

    if (args.active) {
      active_kms_data.active_state = *args.active;
    }

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

}  // namespace android
