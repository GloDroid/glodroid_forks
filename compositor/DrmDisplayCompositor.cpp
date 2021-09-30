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

DrmDisplayCompositor::DrmDisplayCompositor()
    : resource_manager_(nullptr),
      display_(-1),
      initialized_(false),
      active_(false),
      use_hw_overlays_(true) {
}

DrmDisplayCompositor::~DrmDisplayCompositor() {
  if (!initialized_)
    return;

  active_composition_.reset();
}

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

std::tuple<uint32_t, uint32_t, int>
DrmDisplayCompositor::GetActiveModeResolution() {
  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  DrmConnector *connector = drm->GetConnectorForDisplay(display_);
  if (connector == nullptr) {
    ALOGE("Failed to determine display mode: no connector for display %d",
          display_);
    return std::make_tuple(0, 0, -ENODEV);
  }

  const DrmMode &mode = connector->active_mode();
  return std::make_tuple(mode.h_display(), mode.v_display(), 0);
}

int DrmDisplayCompositor::DisablePlanes(DrmDisplayComposition *display_comp) {
  auto pset = MakeDrmModeAtomicReqUnique();
  if (!pset) {
    ALOGE("Failed to allocate property set");
    return -ENOMEM;
  }

  int ret = 0;
  std::vector<DrmCompositionPlane> &comp_planes = display_comp
                                                      ->composition_planes();
  for (DrmCompositionPlane &comp_plane : comp_planes) {
    if (comp_plane.plane()->AtomicDisablePlane(*pset) != 0) {
      return -EINVAL;
    }
  }
  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  ret = drmModeAtomicCommit(drm->fd(), pset.get(), 0, drm);
  if (ret) {
    ALOGE("Failed to commit pset ret=%d\n", ret);
    return ret;
  }

  return 0;
}

int DrmDisplayCompositor::CommitFrame(DrmDisplayComposition *display_comp,
                                      bool test_only) {
  ATRACE_CALL();

  int ret = 0;

  std::vector<DrmHwcLayer> &layers = display_comp->layers();
  std::vector<DrmCompositionPlane> &comp_planes = display_comp
                                                      ->composition_planes();
  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  uint64_t out_fences[drm->crtcs().size()];

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

  if (crtc->out_fence_ptr_property() &&
      !crtc->out_fence_ptr_property()
           .AtomicSet(*pset, (uint64_t)&out_fences[crtc->pipe()])) {
    return -EINVAL;
  }

  if (mode_.blob &&
      (!crtc->active_property().AtomicSet(*pset, 1) ||
       !crtc->mode_property().AtomicSet(*pset, *mode_.blob) ||
       !connector->crtc_id_property().AtomicSet(*pset, crtc->id()))) {
    return -EINVAL;
  }

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

  if (!ret) {
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    if (test_only)
      flags |= DRM_MODE_ATOMIC_TEST_ONLY;

    ret = drmModeAtomicCommit(drm->fd(), pset.get(), flags, drm);
    if (ret) {
      if (!test_only)
        ALOGE("Failed to commit pset ret=%d\n", ret);
      return ret;
    }
  }

  if (!test_only && mode_.blob) {
    /* TODO: Add dpms to the pset when the kernel supports it */
    ret = ApplyDpms(display_comp);
    if (ret) {
      ALOGE("Failed to apply DPMS after modeset %d\n", ret);
      return ret;
    }

    connector->set_active_mode(mode_.mode);
    mode_.old_blob = std::move(mode_.blob);
  }

  if (crtc->out_fence_ptr_property()) {
    display_comp->out_fence_ = UniqueFd((int)out_fences[crtc->pipe()]);
  }

  return ret;
}

int DrmDisplayCompositor::ApplyDpms(DrmDisplayComposition *display_comp) {
  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  DrmConnector *conn = drm->GetConnectorForDisplay(display_);
  if (!conn) {
    ALOGE("Failed to get DrmConnector for display %d", display_);
    return -ENODEV;
  }

  const DrmProperty &prop = conn->dpms_property();
  int ret = drmModeConnectorSetProperty(drm->fd(), conn->id(), prop.id(),
                                        display_comp->dpms_mode());
  if (ret) {
    ALOGE("Failed to set DPMS property for connector %d", conn->id());
    return ret;
  }
  return 0;
}

auto DrmDisplayCompositor::CreateModeBlob(const DrmMode &mode)
    -> DrmModeUserPropertyBlobUnique {
  struct drm_mode_modeinfo drm_mode {};
  mode.ToDrmModeModeInfo(&drm_mode);

  DrmDevice *drm = resource_manager_->GetDrmDevice(display_);
  return drm->RegisterUserPropertyBlob(&drm_mode,
                                       sizeof(struct drm_mode_modeinfo));
}

void DrmDisplayCompositor::ClearDisplay() {
  if (!active_composition_)
    return;

  if (DisablePlanes(active_composition_.get()))
    return;

  active_composition_.reset(nullptr);
}

void DrmDisplayCompositor::ApplyFrame(
    std::unique_ptr<DrmDisplayComposition> composition, int status) {
  int ret = status;

  if (!ret) {
    ret = CommitFrame(composition.get(), false);
  }

  if (ret) {
    ALOGE("Composite failed for display %d", display_);
    // Disable the hw used by the last active composition. This allows us to
    // signal the release fences from that composition to avoid hanging.
    ClearDisplay();
    return;
  }

  active_composition_.swap(composition);
}

int DrmDisplayCompositor::ApplyComposition(
    std::unique_ptr<DrmDisplayComposition> composition) {
  int ret = 0;
  switch (composition->type()) {
    case DRM_COMPOSITION_TYPE_FRAME:
      if (composition->geometry_changed()) {
        // Send the composition to the kernel to ensure we can commit it. This
        // is just a test, it won't actually commit the frame.
        ret = CommitFrame(composition.get(), true);
        if (ret) {
          ALOGE("Commit test failed for display %d, FIXME", display_);
          return ret;
        }
      }

      ApplyFrame(std::move(composition), ret);
      break;
    case DRM_COMPOSITION_TYPE_DPMS:
      active_ = (composition->dpms_mode() == DRM_MODE_DPMS_ON);
      ret = ApplyDpms(composition.get());
      if (ret)
        ALOGE("Failed to apply dpms for display %d", display_);
      return ret;
    case DRM_COMPOSITION_TYPE_MODESET:
      mode_.mode = composition->display_mode();
      mode_.blob = CreateModeBlob(mode_.mode);
      if (!mode_.blob) {
        ALOGE("Failed to create mode blob for display %d", display_);
        return -EINVAL;
      }
      return 0;
    default:
      ALOGE("Unknown composition type %d", composition->type());
      return -EINVAL;
  }

  return ret;
}

int DrmDisplayCompositor::TestComposition(DrmDisplayComposition *composition) {
  return CommitFrame(composition, true);
}

}  // namespace android
