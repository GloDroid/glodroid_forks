/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ANDROID_DRM_HWC_TWO_H_
#define ANDROID_DRM_HWC_TWO_H_

#include <hardware/hwcomposer2.h>

#include "drm/ResourceManager.h"
#include "hwc2_device/HwcDisplay.h"

namespace android {

class DrmHwcTwo {
 public:
  DrmHwcTwo();

  HWC2::Error Init();

  std::pair<HWC2_PFN_HOTPLUG, hwc2_callback_data_t> hotplug_callback_{};
  std::pair<HWC2_PFN_VSYNC, hwc2_callback_data_t> vsync_callback_{};
#if PLATFORM_SDK_VERSION > 29
  std::pair<HWC2_PFN_VSYNC_2_4, hwc2_callback_data_t> vsync_2_4_callback_{};
#endif
  std::pair<HWC2_PFN_REFRESH, hwc2_callback_data_t> refresh_callback_{};

  static HwcDisplay *GetDisplay(DrmHwcTwo *hwc, hwc2_display_t display_handle) {
    auto it = hwc->displays_.find(display_handle);
    if (it == hwc->displays_.end())
      return nullptr;

    return &it->second;
  }

  // Device functions
  HWC2::Error CreateVirtualDisplay(uint32_t width, uint32_t height,
                                   int32_t *format, hwc2_display_t *display);
  HWC2::Error DestroyVirtualDisplay(hwc2_display_t display);
  void Dump(uint32_t *outSize, char *outBuffer);
  uint32_t GetMaxVirtualDisplayCount();
  HWC2::Error RegisterCallback(int32_t descriptor, hwc2_callback_data_t data,
                               hwc2_function_pointer_t function);
  HWC2::Error CreateDisplay(hwc2_display_t displ, HWC2::DisplayType type);

  auto &GetResMan() {
    return resource_manager_;
  }

 private:
  void HandleDisplayHotplug(hwc2_display_t displayid, int state);
  void HandleInitialHotplugState(DrmDevice *drmDevice);

  void HandleHotplugUEvent();

  ResourceManager resource_manager_;
  std::map<hwc2_display_t, HwcDisplay> displays_;

  std::string mDumpString;
};
}  // namespace android

#endif
