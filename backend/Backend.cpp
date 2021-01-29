/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "Backend.h"

#include <climits>

#include "BackendManager.h"
#include "bufferinfo/BufferInfoGetter.h"

namespace android {

HWC2::Error Backend::ValidateDisplay(DrmHwcTwo::HwcDisplay *display,
                                     uint32_t *num_types,
                                     uint32_t *num_requests) {
  *num_types = 0;
  *num_requests = 0;

  std::map<uint32_t, DrmHwcTwo::HwcLayer *> z_map;
  std::map<uint32_t, DrmHwcTwo::HwcLayer *> z_map_tmp;
  uint32_t z_index = 0;
  // First create a map of layers and z_order values
  for (std::pair<const hwc2_layer_t, DrmHwcTwo::HwcLayer> &l :
       display->layers())
    z_map_tmp.emplace(std::make_pair(l.second.z_order(), &l.second));
  // normalise the map so that the lowest z_order layer has key 0
  for (std::pair<const uint32_t, DrmHwcTwo::HwcLayer *> &l : z_map_tmp)
    z_map.emplace(std::make_pair(z_index++, l.second));

  int client_start = -1;
  size_t client_size = 0;

  if (display->compositor().ShouldFlattenOnClient()) {
    client_start = 0;
    client_size = z_map.size();
    MarkValidated(z_map, client_start, client_size);
  } else {
    std::tie(client_start, client_size) = GetClientLayers(display, z_map);

    MarkValidated(z_map, client_start, client_size);

    bool testing_needed = !(client_start == 0 && client_size == z_map.size());

    if (testing_needed &&
        display->CreateComposition(true) != HWC2::Error::None) {
      ++display->total_stats().failed_kms_validate_;
      client_start = 0;
      client_size = z_map.size();
      MarkValidated(z_map, 0, client_size);
    }
  }

  *num_types = client_size;

  display->total_stats().frames_flattened_ = display->compositor()
                                                 .GetFlattenedFramesCount();
  display->total_stats().gpu_pixops_ += CalcPixOps(z_map, client_start,
                                                   client_size);
  display->total_stats().total_pixops_ += CalcPixOps(z_map, 0, z_map.size());

  return *num_types ? HWC2::Error::HasChanges : HWC2::Error::None;
}

std::tuple<int, size_t> Backend::GetClientLayers(
    DrmHwcTwo::HwcDisplay *display,
    const std::vector<DrmHwcTwo::HwcLayer *> &layers) {
  int client_start = -1;
  size_t client_size = 0;

  for (const auto &[z_order, layer] : z_map) {
    if (IsClientLayer(display, layer)) {
      if (client_start < 0)
        client_start = (int)z_order;
      client_size = (z_order - client_start) + 1;
    }
  }

  return GetExtraClientRange(display, z_map, client_start, client_size);
}

bool Backend::IsClientLayer(DrmHwcTwo::HwcDisplay *display,
                            DrmHwcTwo::HwcLayer *layer) {
  return !HardwareSupportsLayerType(layer->sf_type()) ||
         !BufferInfoGetter::GetInstance()->IsHandleUsable(layer->buffer()) ||
         display->color_transform_hint() != HAL_COLOR_TRANSFORM_IDENTITY ||
         (layer->RequireScalingOrPhasing() &&
          display->resource_manager()->ForcedScalingWithGpu());
}

bool Backend::HardwareSupportsLayerType(HWC2::Composition comp_type) {
  return comp_type == HWC2::Composition::Device ||
         comp_type == HWC2::Composition::Cursor;
}

uint32_t Backend::CalcPixOps(const std::vector<DrmHwcTwo::HwcLayer *> &layers,
                             size_t first_z, size_t size) {
  uint32_t pixops = 0;
  for (auto & [ z_order, layer ] : z_map) {
    if (z_order >= first_z && z_order < first_z + size) {
      hwc_rect_t df = layer->display_frame();
      pixops += (df.right - df.left) * (df.bottom - df.top);
    }
  }
  return pixops;
}

void Backend::MarkValidated(std::map<uint32_t, DrmHwcTwo::HwcLayer *> &z_map,
                            size_t client_first_z, size_t client_size) {
  for (std::pair<const uint32_t, DrmHwcTwo::HwcLayer *> &l : z_map) {
    if (l.first >= client_first_z && l.first < client_first_z + client_size)
      l.second->set_validated_type(HWC2::Composition::Client);
    else
      l.second->set_validated_type(HWC2::Composition::Device);
  }
}

std::tuple<int, int> Backend::GetExtraClientRange(
    DrmHwcTwo::HwcDisplay *display,
    const std::vector<DrmHwcTwo::HwcLayer *> &layers, int client_start,
    size_t client_size) {
  size_t avail_planes = display->primary_planes().size() +
                        display->overlay_planes().size();

  /*
   * If more layers then planes, save one plane
   * for client composited layers
   */
  if (avail_planes < display->layers().size())
    avail_planes--;

  size_t extra_client = (layers.size() - client_size) - avail_planes;

  if (extra_client > 0) {
    int start = 0;
    size_t steps = 0;
    if (client_size != 0) {
      size_t prepend = std::min((size_t)client_start, extra_client);
      size_t append = std::min(layers.size() - (client_start + client_size),
                               extra_client);
      start = client_start - (int)prepend;
      client_size += extra_client;
      steps = 1 + std::min(std::min(append, prepend),
                           int(z_map.size()) - (start + client_size));
    } else {
      client_size = extra_client;
      steps = 1 + z_map.size() - extra_client;
    }

    uint32_t gpu_pixops = INT_MAX;
    for (int i = 0; i < steps; i++) {
      uint32_t po = CalcPixOps(z_map, start + i, client_size);
      if (po < gpu_pixops) {
        gpu_pixops = po;
        client_start = start + i;
      }
    }
  }

  return std::make_tuple(client_start, client_size);
}

// clang-format off
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)
REGISTER_BACKEND("generic", Backend);
// clang-format on

}  // namespace android
