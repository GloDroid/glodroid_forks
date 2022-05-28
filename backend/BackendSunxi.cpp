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

#include "BackendSunxi.h"

#include "BackendManager.h"
#include "bufferinfo/BufferInfoGetter.h"
#include "drm_fourcc.h"

namespace android {

bool BackendSunxi::IsClientLayer(HwcDisplay *display, HwcLayer *layer,
                                 bool most_bottom) {
  if (Backend::IsClientLayer(display, layer, most_bottom))
    return true;

  auto &ld = layer->GetLayerData();

  float src_width{};
  float src_height{};
  float dst_width{};
  float dst_height{};

  ld.pi.GetSrcSize(&src_width, &src_height);
  ld.pi.GetDstSize(&dst_width, &dst_height);

  float width_ratio = src_width / dst_width;
  float height_ratio = src_height / dst_height;

  /* VI layer scaler can support downscale up to 16x  */
  constexpr float kViMaxRatio = 15.0;
  bool fits_vi_layer = std::max(width_ratio, height_ratio) <= kViMaxRatio;

  constexpr float kUiMaxRatio = 3.8;
  bool fits_ui_layer = std::max(width_ratio, height_ratio) <= kUiMaxRatio;
//  bool fits_ui_layer = !ld.pi.RequireScalingOrPhasing();

  return (!fits_vi_layer && most_bottom) || (!fits_ui_layer && !most_bottom);
}

// clang-format off
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)
REGISTER_BACKEND("sun4i-drm", BackendSunxi);
// clang-format on

}  // namespace android