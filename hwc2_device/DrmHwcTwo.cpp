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

#define LOG_TAG "hwc-drm-two"

#include "DrmHwcTwo.h"

#include "backend/Backend.h"
#include "utils/log.h"

namespace android {

DrmHwcTwo::DrmHwcTwo() = default;

HWC2::Error DrmHwcTwo::CreateDisplay(hwc2_display_t displ,
                                     HWC2::DisplayType type) {
  DrmDevice *drm = resource_manager_.GetDrmDevice(static_cast<int>(displ));
  if (!drm) {
    ALOGE("Failed to get a valid drmresource");
    return HWC2::Error::NoResources;
  }
  displays_.emplace(std::piecewise_construct, std::forward_as_tuple(displ),
                    std::forward_as_tuple(&resource_manager_, drm, displ, type,
                                          this));

  DrmCrtc *crtc = drm->GetCrtcForDisplay(static_cast<int>(displ));
  if (!crtc) {
    ALOGE("Failed to get crtc for display %d", static_cast<int>(displ));
    return HWC2::Error::BadDisplay;
  }
  auto display_planes = std::vector<DrmPlane *>();
  for (const auto &plane : drm->planes()) {
    if (plane->GetCrtcSupported(*crtc))
      display_planes.push_back(plane.get());
  }
  displays_.at(displ).Init(&display_planes);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::Init() {
  int rv = resource_manager_.Init();
  if (rv) {
    ALOGE("Can't initialize the resource manager %d", rv);
    return HWC2::Error::NoResources;
  }

  HWC2::Error ret = HWC2::Error::None;
  for (int i = 0; i < resource_manager_.GetDisplayCount(); i++) {
    ret = CreateDisplay(i, HWC2::DisplayType::Physical);
    if (ret != HWC2::Error::None) {
      ALOGE("Failed to create display %d with error %d", i, ret);
      return ret;
    }
  }

  resource_manager_.GetUEventListener()->RegisterHotplugHandler([this] {
    const std::lock_guard<std::mutex> lock(GetResMan().GetMainLock());

    HandleHotplugUEvent();
  });

  return ret;
}

HWC2::Error DrmHwcTwo::CreateVirtualDisplay(uint32_t /*width*/,
                                            uint32_t /*height*/,
                                            int32_t * /*format*/,
                                            hwc2_display_t * /*display*/) {
  // TODO(nobody): Implement virtual display
  return HWC2::Error::Unsupported;
}

HWC2::Error DrmHwcTwo::DestroyVirtualDisplay(hwc2_display_t /*display*/) {
  // TODO(nobody): Implement virtual display
  return HWC2::Error::Unsupported;
}

void DrmHwcTwo::Dump(uint32_t *outSize, char *outBuffer) {
  if (outBuffer != nullptr) {
    auto copied_bytes = mDumpString.copy(outBuffer, *outSize);
    *outSize = static_cast<uint32_t>(copied_bytes);
    return;
  }

  std::stringstream output;

  output << "-- drm_hwcomposer --\n\n";

  for (std::pair<const hwc2_display_t, HwcDisplay> &dp : displays_)
    output << dp.second.Dump();

  mDumpString = output.str();
  *outSize = static_cast<uint32_t>(mDumpString.size());
}

uint32_t DrmHwcTwo::GetMaxVirtualDisplayCount() {
  // TODO(nobody): Implement virtual display
  return 0;
}

HWC2::Error DrmHwcTwo::RegisterCallback(int32_t descriptor,
                                        hwc2_callback_data_t data,
                                        hwc2_function_pointer_t function) {
  switch (static_cast<HWC2::Callback>(descriptor)) {
    case HWC2::Callback::Hotplug: {
      hotplug_callback_ = std::make_pair(HWC2_PFN_HOTPLUG(function), data);
      const auto &drm_devices = resource_manager_.GetDrmDevices();
      for (const auto &device : drm_devices)
        HandleInitialHotplugState(device.get());
      break;
    }
    case HWC2::Callback::Refresh: {
      refresh_callback_ = std::make_pair(HWC2_PFN_REFRESH(function), data);
      break;
    }
    case HWC2::Callback::Vsync: {
      vsync_callback_ = std::make_pair(HWC2_PFN_VSYNC(function), data);
      break;
    }
#if PLATFORM_SDK_VERSION > 29
    case HWC2::Callback::Vsync_2_4: {
      vsync_2_4_callback_ = std::make_pair(HWC2_PFN_VSYNC_2_4(function), data);
      break;
    }
#endif
    default:
      break;
  }
  return HWC2::Error::None;
}

void DrmHwcTwo::HandleDisplayHotplug(hwc2_display_t displayid, int state) {
  auto &mutex = GetResMan().GetMainLock();
  if (mutex.try_lock()) {
    ALOGE("FIXME!!!: Main mutex must be locked in %s", __func__);
    mutex.unlock();
    return;
  }

  auto hc = hotplug_callback_;
  if (hc.first != nullptr && hc.second != nullptr) {
    /* For some reason CLIENT will call HWC2 API in hotplug callback handler,
     * which will cause deadlock . Unlock main mutex to prevent this.
     */
    mutex.unlock();
    hc.first(hc.second, displayid,
             state == DRM_MODE_CONNECTED ? HWC2_CONNECTION_CONNECTED
                                         : HWC2_CONNECTION_DISCONNECTED);
    mutex.lock();
  }
}

void DrmHwcTwo::HandleInitialHotplugState(DrmDevice *drmDevice) {
  for (const auto &conn : drmDevice->connectors()) {
    int display_id = conn->display();
    auto &display = displays_.at(display_id);

    if (conn->state() != DRM_MODE_CONNECTED && !display.IsInHeadlessMode())
      continue;
    HandleDisplayHotplug(conn->display(), display.IsInHeadlessMode()
                                              ? DRM_MODE_CONNECTED
                                              : conn->state());
  }
}

void DrmHwcTwo::HandleHotplugUEvent() {
  for (const auto &drm : resource_manager_.GetDrmDevices()) {
    for (const auto &conn : drm->connectors()) {
      drmModeConnection old_state = conn->state();
      drmModeConnection cur_state = conn->UpdateModes()
                                        ? DRM_MODE_UNKNOWNCONNECTION
                                        : conn->state();

      if (cur_state == old_state)
        continue;

      ALOGI("%s event for connector %u on display %d",
            cur_state == DRM_MODE_CONNECTED ? "Plug" : "Unplug", conn->id(),
            conn->display());

      int display_id = conn->display();
      auto &display = displays_.at(display_id);
      display.ChosePreferredConfig();
      if (cur_state != DRM_MODE_CONNECTED) {
        display.ClearDisplay();
      }

      HandleDisplayHotplug(display_id, display.IsInHeadlessMode()
                                           ? DRM_MODE_CONNECTED
                                           : cur_state);
    }
  }
}

}  // namespace android
