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

#ifndef ANDROID_DRM_PLANE_H_
#define ANDROID_DRM_PLANE_H_

#include <stdint.h>
#include <xf86drmMode.h>

#include <vector>

#include "DrmCrtc.h"
#include "DrmProperty.h"
#include "drmhwcomposer.h"

namespace android {

class DrmDevice;

class DrmPlane {
 public:
  DrmPlane(DrmDevice *drm, drmModePlanePtr p);
  DrmPlane(const DrmPlane &) = delete;
  DrmPlane &operator=(const DrmPlane &) = delete;

  int Init();

  uint32_t id() const;

  bool GetCrtcSupported(const DrmCrtc &crtc) const;
  bool IsValidForLayer(DrmHwcLayer *layer);

  uint32_t type() const;

  bool IsFormatSupported(uint32_t format) const;
  bool HasNonRgbFormat() const;

  auto AtomicSetState(drmModeAtomicReq &pset, DrmHwcLayer &layer, uint32_t zpos,
                      uint32_t crtc_id) -> int;
  auto AtomicDisablePlane(drmModeAtomicReq &pset) -> int;
  const DrmProperty &zpos_property() const;

 private:
  DrmDevice *drm_;
  uint32_t id_;

  enum class Presence { kOptional, kMandatory };

  auto GetPlaneProperty(const char *prop_name, DrmProperty &property,
                        Presence presence = Presence::kMandatory) -> bool;

  uint32_t possible_crtc_mask_;

  uint32_t type_{};

  std::vector<uint32_t> formats_;

  DrmProperty crtc_property_;
  DrmProperty fb_property_;
  DrmProperty crtc_x_property_;
  DrmProperty crtc_y_property_;
  DrmProperty crtc_w_property_;
  DrmProperty crtc_h_property_;
  DrmProperty src_x_property_;
  DrmProperty src_y_property_;
  DrmProperty src_w_property_;
  DrmProperty src_h_property_;
  DrmProperty zpos_property_;
  DrmProperty rotation_property_;
  DrmProperty alpha_property_;
  DrmProperty blend_property_;
  DrmProperty in_fence_fd_property_;
  DrmProperty color_encoding_propery_;
  DrmProperty color_range_property_;

  std::map<DrmHwcBlending, uint64_t> blending_enum_map_;
  std::map<DrmHwcColorSpace, uint64_t> color_encoding_enum_map_;
  std::map<DrmHwcSampleRange, uint64_t> color_range_enum_map_;
  std::map<DrmHwcTransform, uint64_t> transform_enum_map_;
};
}  // namespace android

#endif  // ANDROID_DRM_PLANE_H_
