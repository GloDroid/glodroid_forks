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

#ifndef ANDROID_DRM_DISPLAY_COMPOSITION_H_
#define ANDROID_DRM_DISPLAY_COMPOSITION_H_

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>

#include <sstream>
#include <vector>

#include "drm/DrmCrtc.h"
#include "drm/DrmPlane.h"
#include "drmhwcomposer.h"

namespace android {

class Importer;

constexpr size_t kUndefinedSourceLayer = UINT16_MAX;

class DrmCompositionPlane {
 public:
  DrmCompositionPlane() = default;
  DrmCompositionPlane(DrmCompositionPlane &&rhs) = default;
  DrmCompositionPlane &operator=(DrmCompositionPlane &&other) = default;
  DrmCompositionPlane(DrmPlane *plane, size_t source_layer)
      : plane_(plane), source_layer_(source_layer) {
  }

  DrmPlane *plane() const {
    return plane_;
  }

  size_t source_layer() const {
    return source_layer_;
  }

 private:
  DrmPlane *plane_ = nullptr;
  size_t source_layer_ = kUndefinedSourceLayer;
};

class DrmDisplayComposition {
 public:
  DrmDisplayComposition(const DrmDisplayComposition &) = delete;
  explicit DrmDisplayComposition(DrmCrtc *crtc);
  ~DrmDisplayComposition() = default;

  int SetLayers(DrmHwcLayer *layers, size_t num_layers);
  int AddPlaneComposition(DrmCompositionPlane plane);

  int Plan(std::vector<DrmPlane *> *primary_planes,
           std::vector<DrmPlane *> *overlay_planes);

  std::vector<DrmHwcLayer> &layers() {
    return layers_;
  }

  std::vector<DrmCompositionPlane> &composition_planes() {
    return composition_planes_;
  }

  DrmCrtc *crtc() const {
    return crtc_;
  }

 private:
  DrmCrtc *crtc_ = nullptr;

  std::vector<DrmHwcLayer> layers_;
  std::vector<DrmCompositionPlane> composition_planes_;
};
}  // namespace android

#endif  // ANDROID_DRM_DISPLAY_COMPOSITION_H_
