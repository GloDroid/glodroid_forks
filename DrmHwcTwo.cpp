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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#define LOG_TAG "hwc-drm-two"

#include "DrmHwcTwo.h"

#include <fcntl.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer2.h>
#include <sync/sync.h>
#include <unistd.h>

#include <cinttypes>
#include <iostream>
#include <sstream>
#include <string>

#include "backend/BackendManager.h"
#include "bufferinfo/BufferInfoGetter.h"
#include "compositor/DrmDisplayComposition.h"
#include "utils/log.h"
#include "utils/properties.h"

namespace android {

DrmHwcTwo::DrmHwcTwo() : hwc2_device() {
  common.tag = HARDWARE_DEVICE_TAG;
  common.version = HWC_DEVICE_API_VERSION_2_0;
  common.close = HookDevClose;
  getCapabilities = HookDevGetCapabilities;
  getFunction = HookDevGetFunction;
}

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
  for (int i = 0; i < resource_manager_.getDisplayCount(); i++) {
    ret = CreateDisplay(i, HWC2::DisplayType::Physical);
    if (ret != HWC2::Error::None) {
      ALOGE("Failed to create display %d with error %d", i, ret);
      return ret;
    }
  }

  resource_manager_.GetUEventListener()->RegisterHotplugHandler(
      [this] { HandleHotplugUEvent(); });

  return ret;
}

template <typename... Args>
static inline HWC2::Error unsupported(char const *func, Args... /*args*/) {
  ALOGV("Unsupported function: %s", func);
  return HWC2::Error::Unsupported;
}

static inline void supported(char const *func) {
  ALOGV("Supported function: %s", func);
}

HWC2::Error DrmHwcTwo::CreateVirtualDisplay(uint32_t width, uint32_t height,
                                            int32_t *format,
                                            hwc2_display_t *display) {
  // TODO(nobody): Implement virtual display
  return unsupported(__func__, width, height, format, display);
}

HWC2::Error DrmHwcTwo::DestroyVirtualDisplay(hwc2_display_t display) {
  // TODO(nobody): Implement virtual display
  return unsupported(__func__, display);
}

std::string DrmHwcTwo::HwcDisplay::DumpDelta(
    DrmHwcTwo::HwcDisplay::Stats delta) {
  if (delta.total_pixops_ == 0)
    return "No stats yet";
  double ratio = 1.0 - double(delta.gpu_pixops_) / double(delta.total_pixops_);

  std::stringstream ss;
  ss << " Total frames count: " << delta.total_frames_ << "\n"
     << " Failed to test commit frames: " << delta.failed_kms_validate_ << "\n"
     << " Failed to commit frames: " << delta.failed_kms_present_ << "\n"
     << ((delta.failed_kms_present_ > 0)
             ? " !!! Internal failure, FIX it please\n"
             : "")
     << " Flattened frames: " << delta.frames_flattened_ << "\n"
     << " Pixel operations (free units)"
     << " : [TOTAL: " << delta.total_pixops_ << " / GPU: " << delta.gpu_pixops_
     << "]\n"
     << " Composition efficiency: " << ratio;

  return ss.str();
}

std::string DrmHwcTwo::HwcDisplay::Dump() {
  std::string flattening_state_str;
  switch (flattenning_state_) {
    case ClientFlattenningState::Disabled:
      flattening_state_str = "Disabled";
      break;
    case ClientFlattenningState::NotRequired:
      flattening_state_str = "Not needed";
      break;
    case ClientFlattenningState::Flattened:
      flattening_state_str = "Active";
      break;
    case ClientFlattenningState::ClientRefreshRequested:
      flattening_state_str = "Refresh requested";
      break;
    default:
      flattening_state_str = std::to_string(flattenning_state_) +
                             " VSync remains";
  }

  std::stringstream ss;
  ss << "- Display on: " << connector_->name() << "\n"
     << "  Flattening state: " << flattening_state_str << "\n"
     << "Statistics since system boot:\n"
     << DumpDelta(total_stats_) << "\n\n"
     << "Statistics since last dumpsys request:\n"
     << DumpDelta(total_stats_.minus(prev_stats_)) << "\n\n";

  memcpy(&prev_stats_, &total_stats_, sizeof(Stats));
  return ss.str();
}

void DrmHwcTwo::Dump(uint32_t *outSize, char *outBuffer) {
  supported(__func__);

  if (outBuffer != nullptr) {
    auto copied_bytes = mDumpString.copy(outBuffer, *outSize);
    *outSize = static_cast<uint32_t>(copied_bytes);
    return;
  }

  std::stringstream output;

  output << "-- drm_hwcomposer --\n\n";

  for (std::pair<const hwc2_display_t, DrmHwcTwo::HwcDisplay> &dp : displays_)
    output << dp.second.Dump();

  mDumpString = output.str();
  *outSize = static_cast<uint32_t>(mDumpString.size());
}

uint32_t DrmHwcTwo::GetMaxVirtualDisplayCount() {
  // TODO(nobody): Implement virtual display
  unsupported(__func__);
  return 0;
}

HWC2::Error DrmHwcTwo::RegisterCallback(int32_t descriptor,
                                        hwc2_callback_data_t data,
                                        hwc2_function_pointer_t function) {
  supported(__func__);

  std::unique_lock<std::mutex> lock(callback_lock_);

  switch (static_cast<HWC2::Callback>(descriptor)) {
    case HWC2::Callback::Hotplug: {
      hotplug_callback_ = std::make_pair(HWC2_PFN_HOTPLUG(function), data);
      lock.unlock();
      const auto &drm_devices = resource_manager_.getDrmDevices();
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

DrmHwcTwo::HwcDisplay::HwcDisplay(ResourceManager *resource_manager,
                                  DrmDevice *drm, hwc2_display_t handle,
                                  HWC2::DisplayType type, DrmHwcTwo *hwc2)
    : hwc2_(hwc2),
      resource_manager_(resource_manager),
      drm_(drm),
      handle_(handle),
      type_(type),
      color_transform_hint_(HAL_COLOR_TRANSFORM_IDENTITY) {
  supported(__func__);

  // clang-format off
  color_transform_matrix_ = {1.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 0.0,
                             0.0, 0.0, 0.0, 1.0};
  // clang-format on
}

void DrmHwcTwo::HwcDisplay::ClearDisplay() {
  AtomicCommitArgs a_args = {.clear_active_composition = true};
  compositor_.ExecuteAtomicCommit(a_args);
}

HWC2::Error DrmHwcTwo::HwcDisplay::Init(std::vector<DrmPlane *> *planes) {
  supported(__func__);
  planner_ = Planner::CreateInstance(drm_);
  if (!planner_) {
    ALOGE("Failed to create planner instance for composition");
    return HWC2::Error::NoResources;
  }

  int display = static_cast<int>(handle_);
  int ret = compositor_.Init(resource_manager_, display);
  if (ret) {
    ALOGE("Failed display compositor init for display %d (%d)", display, ret);
    return HWC2::Error::NoResources;
  }

  // Split up the given display planes into primary and overlay to properly
  // interface with the composition
  char use_overlay_planes_prop[PROPERTY_VALUE_MAX];
  property_get("vendor.hwc.drm.use_overlay_planes", use_overlay_planes_prop,
               "1");
  bool use_overlay_planes = strtol(use_overlay_planes_prop, nullptr, 10);
  for (auto &plane : *planes) {
    if (plane->type() == DRM_PLANE_TYPE_PRIMARY)
      primary_planes_.push_back(plane);
    else if (use_overlay_planes && (plane)->type() == DRM_PLANE_TYPE_OVERLAY)
      overlay_planes_.push_back(plane);
  }

  crtc_ = drm_->GetCrtcForDisplay(display);
  if (!crtc_) {
    ALOGE("Failed to get crtc for display %d", display);
    return HWC2::Error::BadDisplay;
  }

  connector_ = drm_->GetConnectorForDisplay(display);
  if (!connector_) {
    ALOGE("Failed to get connector for display %d", display);
    return HWC2::Error::BadDisplay;
  }

  ret = vsync_worker_.Init(drm_, display, [this](int64_t timestamp) {
    const std::lock_guard<std::mutex> lock(hwc2_->callback_lock_);
    /* vsync callback */
#if PLATFORM_SDK_VERSION > 29
    if (hwc2_->vsync_2_4_callback_.first != nullptr &&
        hwc2_->vsync_2_4_callback_.second != nullptr) {
      hwc2_vsync_period_t period_ns{};
      GetDisplayVsyncPeriod(&period_ns);
      hwc2_->vsync_2_4_callback_.first(hwc2_->vsync_2_4_callback_.second,
                                       handle_, timestamp, period_ns);
    } else
#endif
        if (hwc2_->vsync_callback_.first != nullptr &&
            hwc2_->vsync_callback_.second != nullptr) {
      hwc2_->vsync_callback_.first(hwc2_->vsync_callback_.second, handle_,
                                   timestamp);
    }
  });
  if (ret) {
    ALOGE("Failed to create event worker for d=%d %d\n", display, ret);
    return HWC2::Error::BadDisplay;
  }

  ret = flattening_vsync_worker_.Init(drm_, display, [this](int64_t /*timestamp*/) {
    const std::lock_guard<std::mutex> lock(hwc2_->callback_lock_);
    /* Frontend flattening */
    if (flattenning_state_ > ClientFlattenningState::ClientRefreshRequested &&
        --flattenning_state_ ==
            ClientFlattenningState::ClientRefreshRequested &&
        hwc2_->refresh_callback_.first != nullptr &&
        hwc2_->refresh_callback_.second != nullptr) {
      hwc2_->refresh_callback_.first(hwc2_->refresh_callback_.second, handle_);
      flattening_vsync_worker_.VSyncControl(false);
    }
  });
  if (ret) {
    ALOGE("Failed to create event worker for d=%d %d\n", display, ret);
    return HWC2::Error::BadDisplay;
  }

  ret = BackendManager::GetInstance().SetBackendForDisplay(this);
  if (ret) {
    ALOGE("Failed to set backend for d=%d %d\n", display, ret);
    return HWC2::Error::BadDisplay;
  }

  client_layer_.SetLayerBlendMode(HWC2_BLEND_MODE_PREMULTIPLIED);

  return ChosePreferredConfig();
}

HWC2::Error DrmHwcTwo::HwcDisplay::ChosePreferredConfig() {
  // Fetch the number of modes from the display
  uint32_t num_configs = 0;
  HWC2::Error err = GetDisplayConfigs(&num_configs, nullptr);
  if (err != HWC2::Error::None || !num_configs)
    return HWC2::Error::BadDisplay;

  return SetActiveConfig(preferred_config_id_);
}

HWC2::Error DrmHwcTwo::HwcDisplay::AcceptDisplayChanges() {
  supported(__func__);
  for (std::pair<const hwc2_layer_t, DrmHwcTwo::HwcLayer> &l : layers_)
    l.second.accept_type_change();
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::CreateLayer(hwc2_layer_t *layer) {
  supported(__func__);
  layers_.emplace(static_cast<hwc2_layer_t>(layer_idx_), HwcLayer());
  *layer = static_cast<hwc2_layer_t>(layer_idx_);
  ++layer_idx_;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::DestroyLayer(hwc2_layer_t layer) {
  supported(__func__);
  if (!get_layer(layer))
    return HWC2::Error::BadLayer;

  layers_.erase(layer);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetActiveConfig(
    hwc2_config_t *config) const {
  supported(__func__);
  if (hwc_configs_.count(active_config_id_) == 0)
    return HWC2::Error::BadConfig;

  *config = active_config_id_;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetChangedCompositionTypes(
    uint32_t *num_elements, hwc2_layer_t *layers, int32_t *types) {
  supported(__func__);
  uint32_t num_changes = 0;
  for (std::pair<const hwc2_layer_t, DrmHwcTwo::HwcLayer> &l : layers_) {
    if (l.second.type_changed()) {
      if (layers && num_changes < *num_elements)
        layers[num_changes] = l.first;
      if (types && num_changes < *num_elements)
        types[num_changes] = static_cast<int32_t>(l.second.validated_type());
      ++num_changes;
    }
  }
  if (!layers && !types)
    *num_elements = num_changes;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetClientTargetSupport(uint32_t width,
                                                          uint32_t height,
                                                          int32_t /*format*/,
                                                          int32_t dataspace) {
  supported(__func__);
  std::pair<uint32_t, uint32_t> min = drm_->min_resolution();
  std::pair<uint32_t, uint32_t> max = drm_->max_resolution();

  if (width < min.first || height < min.second)
    return HWC2::Error::Unsupported;

  if (width > max.first || height > max.second)
    return HWC2::Error::Unsupported;

  if (dataspace != HAL_DATASPACE_UNKNOWN)
    return HWC2::Error::Unsupported;

  // TODO(nobody): Validate format can be handled by either GL or planes
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetColorModes(uint32_t *num_modes,
                                                 int32_t *modes) {
  supported(__func__);
  if (!modes)
    *num_modes = 1;

  if (modes)
    *modes = HAL_COLOR_MODE_NATIVE;

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayAttribute(hwc2_config_t config,
                                                       int32_t attribute_in,
                                                       int32_t *value) {
  supported(__func__);
  int conf = static_cast<int>(config);

  if (hwc_configs_.count(conf) == 0) {
    ALOGE("Could not find active mode for %d", conf);
    return HWC2::Error::BadConfig;
  }

  auto &hwc_config = hwc_configs_[conf];

  static const int32_t kUmPerInch = 25400;
  uint32_t mm_width = connector_->mm_width();
  uint32_t mm_height = connector_->mm_height();
  auto attribute = static_cast<HWC2::Attribute>(attribute_in);
  switch (attribute) {
    case HWC2::Attribute::Width:
      *value = static_cast<int>(hwc_config.mode.h_display());
      break;
    case HWC2::Attribute::Height:
      *value = static_cast<int>(hwc_config.mode.v_display());
      break;
    case HWC2::Attribute::VsyncPeriod:
      // in nanoseconds
      *value = static_cast<int>(1E9 / hwc_config.mode.v_refresh());
      break;
    case HWC2::Attribute::DpiX:
      // Dots per 1000 inches
      *value = mm_width ? static_cast<int>(hwc_config.mode.h_display() *
                                           kUmPerInch / mm_width)
                        : -1;
      break;
    case HWC2::Attribute::DpiY:
      // Dots per 1000 inches
      *value = mm_height ? static_cast<int>(hwc_config.mode.v_display() *
                                            kUmPerInch / mm_height)
                         : -1;
      break;
#if PLATFORM_SDK_VERSION > 29
    case HWC2::Attribute::ConfigGroup:
      /* Dispite ConfigGroup is a part of HWC2.4 API, framework
       * able to request it even if service @2.1 is used */
      *value = hwc_config.group_id;
      break;
#endif
    default:
      *value = -1;
      return HWC2::Error::BadConfig;
  }
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayConfigs(uint32_t *num_configs,
                                                     hwc2_config_t *configs) {
  supported(__func__);
  // Since this callback is normally invoked twice (once to get the count, and
  // once to populate configs), we don't really want to read the edid
  // redundantly. Instead, only update the modes on the first invocation. While
  // it's possible this will result in stale modes, it'll all come out in the
  // wash when we try to set the active config later.
  if (!configs) {
    int ret = connector_->UpdateModes();
    if (ret) {
      ALOGE("Failed to update display modes %d", ret);
      return HWC2::Error::BadDisplay;
    }

    hwc_configs_.clear();
    preferred_config_id_ = 0;
    int preferred_config_group_id_ = 0;

    if (connector_->modes().empty()) {
      ALOGE("No modes reported by KMS");
      return HWC2::Error::BadDisplay;
    }

    int last_config_id = 1;
    int last_group_id = 1;

    /* Group modes */
    for (const auto &mode : connector_->modes()) {
      /* Find group for the new mode or create new group */
      int group_found = 0;
      for (auto &hwc_config : hwc_configs_) {
        if (mode.h_display() == hwc_config.second.mode.h_display() &&
            mode.v_display() == hwc_config.second.mode.v_display()) {
          group_found = hwc_config.second.group_id;
        }
      }
      if (group_found == 0) {
        group_found = last_group_id++;
      }

      bool disabled = false;
      if (mode.flags() & DRM_MODE_FLAG_3D_MASK) {
        ALOGI("Disabling display mode %s (Modes with 3D flag aren't supported)",
              mode.name().c_str());
        disabled = true;
      }

      /* Add config */
      hwc_configs_[last_config_id] = {
          .id = last_config_id,
          .group_id = group_found,
          .mode = mode,
          .disabled = disabled,
      };

      /* Chwck if the mode is preferred */
      if ((mode.type() & DRM_MODE_TYPE_PREFERRED) != 0 &&
          preferred_config_id_ == 0) {
        preferred_config_id_ = last_config_id;
        preferred_config_group_id_ = group_found;
      }

      last_config_id++;
    }

    /* We must have preferred mode. Set first mode as preferred
     * in case KMS haven't reported anything. */
    if (preferred_config_id_ == 0) {
      preferred_config_id_ = 1;
      preferred_config_group_id_ = 1;
    }

    for (int group = 1; group < last_group_id; group++) {
      bool has_interlaced = false;
      bool has_progressive = false;
      for (auto &hwc_config : hwc_configs_) {
        if (hwc_config.second.group_id != group || hwc_config.second.disabled) {
          continue;
        }

        if (hwc_config.second.IsInterlaced()) {
          has_interlaced = true;
        } else {
          has_progressive = true;
        }
      }

      bool has_both = has_interlaced && has_progressive;
      if (!has_both) {
        continue;
      }

      bool group_contains_preferred_interlaced = false;
      if (group == preferred_config_group_id_ &&
          hwc_configs_[preferred_config_id_].IsInterlaced()) {
        group_contains_preferred_interlaced = true;
      }

      for (auto &hwc_config : hwc_configs_) {
        if (hwc_config.second.group_id != group || hwc_config.second.disabled) {
          continue;
        }

        bool disable = group_contains_preferred_interlaced
                           ? !hwc_config.second.IsInterlaced()
                           : hwc_config.second.IsInterlaced();

        if (disable) {
          ALOGI(
              "Group %i: Disabling display mode %s (This group should consist "
              "of %s modes)",
              group, hwc_config.second.mode.name().c_str(),
              group_contains_preferred_interlaced ? "interlaced"
                                                  : "progressive");

          hwc_config.second.disabled = true;
        }
      }
    }

    /* Group should not contain 2 modes with FPS delta less than ~1HZ
     * otherwise android.graphics.cts.SetFrameRateTest CTS will fail
     */
    for (int m1 = 1; m1 < last_config_id; m1++) {
      for (int m2 = 1; m2 < last_config_id; m2++) {
        if (m1 != m2 &&
            hwc_configs_[m1].group_id == hwc_configs_[m2].group_id &&
            !hwc_configs_[m1].disabled && !hwc_configs_[m2].disabled &&
            fabsf(hwc_configs_[m1].mode.v_refresh() -
                  hwc_configs_[m2].mode.v_refresh()) < 1.0) {
          ALOGI(
              "Group %i: Disabling display mode %s (Refresh rate value is "
              "too close to existing mode %s)",
              hwc_configs_[m2].group_id, hwc_configs_[m2].mode.name().c_str(),
              hwc_configs_[m1].mode.name().c_str());

          hwc_configs_[m2].disabled = true;
        }
      }
    }
  }

  uint32_t idx = 0;
  for (auto &hwc_config : hwc_configs_) {
    if (hwc_config.second.disabled) {
      continue;
    }

    if (configs != nullptr) {
      if (idx >= *num_configs) {
        break;
      }
      configs[idx] = hwc_config.second.id;
    }

    idx++;
  }
  *num_configs = idx;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayName(uint32_t *size, char *name) {
  supported(__func__);
  std::ostringstream stream;
  stream << "display-" << connector_->id();
  std::string string = stream.str();
  size_t length = string.length();
  if (!name) {
    *size = length;
    return HWC2::Error::None;
  }

  *size = std::min<uint32_t>(static_cast<uint32_t>(length - 1), *size);
  strncpy(name, string.c_str(), *size);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayRequests(int32_t *display_requests,
                                                      uint32_t *num_elements,
                                                      hwc2_layer_t *layers,
                                                      int32_t *layer_requests) {
  supported(__func__);
  // TODO(nobody): I think virtual display should request
  //      HWC2_DISPLAY_REQUEST_WRITE_CLIENT_TARGET_TO_OUTPUT here
  unsupported(__func__, display_requests, num_elements, layers, layer_requests);
  *num_elements = 0;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayType(int32_t *type) {
  supported(__func__);
  *type = static_cast<int32_t>(type_);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDozeSupport(int32_t *support) {
  supported(__func__);
  *support = 0;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetHdrCapabilities(
    uint32_t *num_types, int32_t * /*types*/, float * /*max_luminance*/,
    float * /*max_average_luminance*/, float * /*min_luminance*/) {
  supported(__func__);
  *num_types = 0;
  return HWC2::Error::None;
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=1767
 */
HWC2::Error DrmHwcTwo::HwcDisplay::GetReleaseFences(uint32_t *num_elements,
                                                    hwc2_layer_t *layers,
                                                    int32_t *fences) {
  supported(__func__);
  uint32_t num_layers = 0;

  for (std::pair<const hwc2_layer_t, DrmHwcTwo::HwcLayer> &l : layers_) {
    ++num_layers;
    if (layers == nullptr || fences == nullptr)
      continue;

    if (num_layers > *num_elements) {
      ALOGW("Overflow num_elements %d/%d", num_layers, *num_elements);
      return HWC2::Error::None;
    }

    layers[num_layers - 1] = l.first;
    fences[num_layers - 1] = l.second.release_fence_.Release();
  }
  *num_elements = num_layers;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::CreateComposition(AtomicCommitArgs &a_args) {
  // order the layers by z-order
  bool use_client_layer = false;
  uint32_t client_z_order = UINT32_MAX;
  std::map<uint32_t, DrmHwcTwo::HwcLayer *> z_map;
  for (std::pair<const hwc2_layer_t, DrmHwcTwo::HwcLayer> &l : layers_) {
    switch (l.second.validated_type()) {
      case HWC2::Composition::Device:
        z_map.emplace(std::make_pair(l.second.z_order(), &l.second));
        break;
      case HWC2::Composition::Client:
        // Place it at the z_order of the lowest client layer
        use_client_layer = true;
        client_z_order = std::min(client_z_order, l.second.z_order());
        break;
      default:
        continue;
    }
  }
  if (use_client_layer)
    z_map.emplace(std::make_pair(client_z_order, &client_layer_));

  if (z_map.empty())
    return HWC2::Error::BadLayer;

  std::vector<DrmHwcLayer> composition_layers;

  // now that they're ordered by z, add them to the composition
  for (std::pair<const uint32_t, DrmHwcTwo::HwcLayer *> &l : z_map) {
    DrmHwcLayer layer;
    l.second->PopulateDrmLayer(&layer);
    int ret = layer.ImportBuffer(drm_);
    if (ret) {
      ALOGE("Failed to import layer, ret=%d", ret);
      return HWC2::Error::NoResources;
    }
    composition_layers.emplace_back(std::move(layer));
  }

  auto composition = std::make_shared<DrmDisplayComposition>(crtc_,
                                                             planner_.get());

  // TODO(nobody): Don't always assume geometry changed
  int ret = composition->SetLayers(composition_layers.data(),
                                   composition_layers.size());
  if (ret) {
    ALOGE("Failed to set layers in the composition ret=%d", ret);
    return HWC2::Error::BadLayer;
  }

  std::vector<DrmPlane *> primary_planes(primary_planes_);
  std::vector<DrmPlane *> overlay_planes(overlay_planes_);
  ret = composition->Plan(&primary_planes, &overlay_planes);
  if (ret) {
    ALOGV("Failed to plan the composition ret=%d", ret);
    return HWC2::Error::BadConfig;
  }

  // Disable the planes we're not using
  for (auto i = primary_planes.begin(); i != primary_planes.end();) {
    composition->AddPlaneDisable(*i);
    i = primary_planes.erase(i);
  }
  for (auto i = overlay_planes.begin(); i != overlay_planes.end();) {
    composition->AddPlaneDisable(*i);
    i = overlay_planes.erase(i);
  }

  a_args.composition = composition;
  ret = compositor_.ExecuteAtomicCommit(a_args);

  if (ret) {
    if (!a_args.test_only)
      ALOGE("Failed to apply the frame composition ret=%d", ret);
    return HWC2::Error::BadParameter;
  }
  return HWC2::Error::None;
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=1805
 */
HWC2::Error DrmHwcTwo::HwcDisplay::PresentDisplay(int32_t *present_fence) {
  supported(__func__);
  HWC2::Error ret;

  ++total_stats_.total_frames_;

  AtomicCommitArgs a_args{};
  ret = CreateComposition(a_args);

  if (ret != HWC2::Error::None)
    ++total_stats_.failed_kms_present_;

  if (ret == HWC2::Error::BadLayer) {
    // Can we really have no client or device layers?
    *present_fence = -1;
    return HWC2::Error::None;
  }
  if (ret != HWC2::Error::None)
    return ret;

  *present_fence = a_args.out_fence.Release();

  ++frame_no_;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetActiveConfig(hwc2_config_t config) {
  supported(__func__);

  int conf = static_cast<int>(config);

  if (hwc_configs_.count(conf) == 0) {
    ALOGE("Could not find active mode for %d", conf);
    return HWC2::Error::BadConfig;
  }

  auto &mode = hwc_configs_[conf].mode;

  AtomicCommitArgs a_args = {
      .display_mode = mode,
      .clear_active_composition = true,
  };

  int err = compositor_.ExecuteAtomicCommit(a_args);
  if (err != 0) {
    ALOGE("Failed to queue mode changing commit %d", err);
    return HWC2::Error::BadConfig;
  }

  active_config_id_ = conf;

  // Setup the client layer's dimensions
  hwc_rect_t display_frame = {.left = 0,
                              .top = 0,
                              .right = static_cast<int>(mode.h_display()),
                              .bottom = static_cast<int>(mode.v_display())};
  client_layer_.SetLayerDisplayFrame(display_frame);

  return HWC2::Error::None;
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=1861
 */
HWC2::Error DrmHwcTwo::HwcDisplay::SetClientTarget(buffer_handle_t target,
                                                   int32_t acquire_fence,
                                                   int32_t dataspace,
                                                   hwc_region_t /*damage*/) {
  supported(__func__);

  client_layer_.set_buffer(target);
  client_layer_.acquire_fence_ = UniqueFd(acquire_fence);
  client_layer_.SetLayerDataspace(dataspace);

  /* TODO: Do not update source_crop every call.
   * It makes sense to do it once after every hotplug event. */
  hwc_drm_bo bo{};
  BufferInfoGetter::GetInstance()->ConvertBoInfo(target, &bo);

  hwc_frect_t source_crop = {.left = 0.0F,
                             .top = 0.0F,
                             .right = static_cast<float>(bo.width),
                             .bottom = static_cast<float>(bo.height)};
  client_layer_.SetLayerSourceCrop(source_crop);

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetColorMode(int32_t mode) {
  supported(__func__);

  if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
    return HWC2::Error::BadParameter;

  if (mode != HAL_COLOR_MODE_NATIVE)
    return HWC2::Error::Unsupported;

  color_mode_ = mode;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetColorTransform(const float *matrix,
                                                     int32_t hint) {
  supported(__func__);
  if (hint < HAL_COLOR_TRANSFORM_IDENTITY ||
      hint > HAL_COLOR_TRANSFORM_CORRECT_TRITANOPIA)
    return HWC2::Error::BadParameter;

  if (!matrix && hint == HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX)
    return HWC2::Error::BadParameter;

  color_transform_hint_ = static_cast<android_color_transform_t>(hint);
  if (color_transform_hint_ == HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX)
    std::copy(matrix, matrix + MATRIX_SIZE, color_transform_matrix_.begin());

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetOutputBuffer(buffer_handle_t buffer,
                                                   int32_t release_fence) {
  supported(__func__);
  // TODO(nobody): Need virtual display support
  return unsupported(__func__, buffer, release_fence);
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetPowerMode(int32_t mode_in) {
  supported(__func__);
  auto mode = static_cast<HWC2::PowerMode>(mode_in);
  AtomicCommitArgs a_args{};

  switch (mode) {
    case HWC2::PowerMode::Off:
      a_args.active = false;
      break;
    case HWC2::PowerMode::On:
      a_args.active = true;
      break;
    case HWC2::PowerMode::Doze:
    case HWC2::PowerMode::DozeSuspend:
      return HWC2::Error::Unsupported;
    default:
      ALOGI("Power mode %d is unsupported\n", mode);
      return HWC2::Error::BadParameter;
  };

  int err = compositor_.ExecuteAtomicCommit(a_args);
  if (err) {
    ALOGE("Failed to apply the dpms composition err=%d", err);
    return HWC2::Error::BadParameter;
  }
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetVsyncEnabled(int32_t enabled) {
  supported(__func__);
  vsync_worker_.VSyncControl(HWC2_VSYNC_ENABLE == enabled);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::ValidateDisplay(uint32_t *num_types,
                                                   uint32_t *num_requests) {
  supported(__func__);

  return backend_->ValidateDisplay(this, num_types, num_requests);
}

std::vector<DrmHwcTwo::HwcLayer *>
DrmHwcTwo::HwcDisplay::GetOrderLayersByZPos() {
  std::vector<DrmHwcTwo::HwcLayer *> ordered_layers;
  ordered_layers.reserve(layers_.size());

  for (auto &[handle, layer] : layers_) {
    ordered_layers.emplace_back(&layer);
  }

  std::sort(std::begin(ordered_layers), std::end(ordered_layers),
            [](const DrmHwcTwo::HwcLayer *lhs, const DrmHwcTwo::HwcLayer *rhs) {
              return lhs->z_order() < rhs->z_order();
            });

  return ordered_layers;
}

#if PLATFORM_SDK_VERSION > 29
HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayConnectionType(uint32_t *outType) {
  if (connector_->internal())
    *outType = static_cast<uint32_t>(HWC2::DisplayConnectionType::Internal);
  else if (connector_->external())
    *outType = static_cast<uint32_t>(HWC2::DisplayConnectionType::External);
  else
    return HWC2::Error::BadConfig;

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayVsyncPeriod(
    hwc2_vsync_period_t *outVsyncPeriod /* ns */) {
  supported(__func__);
  return GetDisplayAttribute(active_config_id_, HWC2_ATTRIBUTE_VSYNC_PERIOD,
                             (int32_t *)(outVsyncPeriod));
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetActiveConfigWithConstraints(
    hwc2_config_t /*config*/,
    hwc_vsync_period_change_constraints_t *vsyncPeriodChangeConstraints,
    hwc_vsync_period_change_timeline_t *outTimeline) {
  supported(__func__);

  if (vsyncPeriodChangeConstraints == nullptr || outTimeline == nullptr) {
    return HWC2::Error::BadParameter;
  }

  return HWC2::Error::BadConfig;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetAutoLowLatencyMode(bool /*on*/) {
  return HWC2::Error::Unsupported;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetSupportedContentTypes(
    uint32_t *outNumSupportedContentTypes,
    const uint32_t *outSupportedContentTypes) {
  if (outSupportedContentTypes == nullptr)
    *outNumSupportedContentTypes = 0;

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetContentType(int32_t contentType) {
  supported(__func__);

  if (contentType != HWC2_CONTENT_TYPE_NONE)
    return HWC2::Error::Unsupported;

  /* TODO: Map to the DRM Connector property:
   * https://elixir.bootlin.com/linux/v5.4-rc5/source/drivers/gpu/drm/drm_connector.c#L809
   */

  return HWC2::Error::None;
}
#endif

#if PLATFORM_SDK_VERSION > 28
HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayIdentificationData(
    uint8_t *outPort, uint32_t *outDataSize, uint8_t *outData) {
  supported(__func__);

  auto blob = connector_->GetEdidBlob();

  if (!blob) {
    ALOGE("Failed to get edid property value.");
    return HWC2::Error::Unsupported;
  }

  if (outData) {
    *outDataSize = std::min(*outDataSize, blob->length);
    memcpy(outData, blob->data, *outDataSize);
  } else {
    *outDataSize = blob->length;
  }
  *outPort = connector_->id();

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayCapabilities(
    uint32_t *outNumCapabilities, uint32_t *outCapabilities) {
  unsupported(__func__, outCapabilities);

  if (outNumCapabilities == nullptr) {
    return HWC2::Error::BadParameter;
  }

  *outNumCapabilities = 0;

  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::GetDisplayBrightnessSupport(
    bool *supported) {
  *supported = false;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetDisplayBrightness(
    float /* brightness */) {
  return HWC2::Error::Unsupported;
}

#endif /* PLATFORM_SDK_VERSION > 28 */

#if PLATFORM_SDK_VERSION > 27

HWC2::Error DrmHwcTwo::HwcDisplay::GetRenderIntents(
    int32_t mode, uint32_t *outNumIntents,
    int32_t * /*android_render_intent_v1_1_t*/ outIntents) {
  if (mode != HAL_COLOR_MODE_NATIVE) {
    return HWC2::Error::BadParameter;
  }

  if (outIntents == nullptr) {
    *outNumIntents = 1;
    return HWC2::Error::None;
  }
  *outNumIntents = 1;
  outIntents[0] = HAL_RENDER_INTENT_COLORIMETRIC;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcDisplay::SetColorModeWithIntent(int32_t mode,
                                                          int32_t intent) {
  if (intent < HAL_RENDER_INTENT_COLORIMETRIC ||
      intent > HAL_RENDER_INTENT_TONE_MAP_ENHANCE)
    return HWC2::Error::BadParameter;

  if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
    return HWC2::Error::BadParameter;

  if (mode != HAL_COLOR_MODE_NATIVE)
    return HWC2::Error::Unsupported;

  if (intent != HAL_RENDER_INTENT_COLORIMETRIC)
    return HWC2::Error::Unsupported;

  color_mode_ = mode;
  return HWC2::Error::None;
}

#endif /* PLATFORM_SDK_VERSION > 27 */

HWC2::Error DrmHwcTwo::HwcLayer::SetCursorPosition(int32_t /*x*/,
                                                   int32_t /*y*/) {
  supported(__func__);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerBlendMode(int32_t mode) {
  supported(__func__);
  switch (static_cast<HWC2::BlendMode>(mode)) {
    case HWC2::BlendMode::None:
      blending_ = DrmHwcBlending::kNone;
      break;
    case HWC2::BlendMode::Premultiplied:
      blending_ = DrmHwcBlending::kPreMult;
      break;
    case HWC2::BlendMode::Coverage:
      blending_ = DrmHwcBlending::kCoverage;
      break;
    default:
      ALOGE("Unknown blending mode b=%d", blending_);
      blending_ = DrmHwcBlending::kNone;
      break;
  }
  return HWC2::Error::None;
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=2314
 */
HWC2::Error DrmHwcTwo::HwcLayer::SetLayerBuffer(buffer_handle_t buffer,
                                                int32_t acquire_fence) {
  supported(__func__);

  set_buffer(buffer);
  acquire_fence_ = UniqueFd(acquire_fence);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerColor(hwc_color_t /*color*/) {
  // TODO(nobody): Put to client composition here?
  supported(__func__);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerCompositionType(int32_t type) {
  sf_type_ = static_cast<HWC2::Composition>(type);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerDataspace(int32_t dataspace) {
  supported(__func__);
  switch (dataspace & HAL_DATASPACE_STANDARD_MASK) {
    case HAL_DATASPACE_STANDARD_BT709:
      color_space_ = DrmHwcColorSpace::kItuRec709;
      break;
    case HAL_DATASPACE_STANDARD_BT601_625:
    case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
    case HAL_DATASPACE_STANDARD_BT601_525:
    case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
      color_space_ = DrmHwcColorSpace::kItuRec601;
      break;
    case HAL_DATASPACE_STANDARD_BT2020:
    case HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
      color_space_ = DrmHwcColorSpace::kItuRec2020;
      break;
    default:
      color_space_ = DrmHwcColorSpace::kUndefined;
  }

  switch (dataspace & HAL_DATASPACE_RANGE_MASK) {
    case HAL_DATASPACE_RANGE_FULL:
      sample_range_ = DrmHwcSampleRange::kFullRange;
      break;
    case HAL_DATASPACE_RANGE_LIMITED:
      sample_range_ = DrmHwcSampleRange::kLimitedRange;
      break;
    default:
      sample_range_ = DrmHwcSampleRange::kUndefined;
  }
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerDisplayFrame(hwc_rect_t frame) {
  supported(__func__);
  display_frame_ = frame;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerPlaneAlpha(float alpha) {
  supported(__func__);
  alpha_ = alpha;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerSidebandStream(
    const native_handle_t *stream) {
  supported(__func__);
  // TODO(nobody): We don't support sideband
  return unsupported(__func__, stream);
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerSourceCrop(hwc_frect_t crop) {
  supported(__func__);
  source_crop_ = crop;
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerSurfaceDamage(hwc_region_t damage) {
  supported(__func__);
  // TODO(nobody): We don't use surface damage, marking as unsupported
  unsupported(__func__, damage);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerTransform(int32_t transform) {
  supported(__func__);

  uint32_t l_transform = 0;

  // 270* and 180* cannot be combined with flips. More specifically, they
  // already contain both horizontal and vertical flips, so those fields are
  // redundant in this case. 90* rotation can be combined with either horizontal
  // flip or vertical flip, so treat it differently
  if (transform == HWC_TRANSFORM_ROT_270) {
    l_transform = DrmHwcTransform::kRotate270;
  } else if (transform == HWC_TRANSFORM_ROT_180) {
    l_transform = DrmHwcTransform::kRotate180;
  } else {
    if (transform & HWC_TRANSFORM_FLIP_H)
      l_transform |= DrmHwcTransform::kFlipH;
    if (transform & HWC_TRANSFORM_FLIP_V)
      l_transform |= DrmHwcTransform::kFlipV;
    if (transform & HWC_TRANSFORM_ROT_90)
      l_transform |= DrmHwcTransform::kRotate90;
  }

  transform_ = static_cast<DrmHwcTransform>(l_transform);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerVisibleRegion(hwc_region_t visible) {
  supported(__func__);
  // TODO(nobody): We don't use this information, marking as unsupported
  unsupported(__func__, visible);
  return HWC2::Error::None;
}

HWC2::Error DrmHwcTwo::HwcLayer::SetLayerZOrder(uint32_t order) {
  supported(__func__);
  z_order_ = order;
  return HWC2::Error::None;
}

void DrmHwcTwo::HwcLayer::PopulateDrmLayer(DrmHwcLayer *layer) {
  supported(__func__);
  layer->sf_handle = buffer_;
  // TODO(rsglobal): Avoid extra fd duplication
  layer->acquire_fence = UniqueFd(fcntl(acquire_fence_.Get(), F_DUPFD_CLOEXEC));
  layer->display_frame = display_frame_;
  layer->alpha = lround(65535.0F * alpha_);
  layer->blending = blending_;
  layer->source_crop = source_crop_;
  layer->transform = transform_;
  layer->color_space = color_space_;
  layer->sample_range = sample_range_;
}

void DrmHwcTwo::HandleDisplayHotplug(hwc2_display_t displayid, int state) {
  const std::lock_guard<std::mutex> lock(callback_lock_);

  if (hotplug_callback_.first != nullptr &&
      hotplug_callback_.second != nullptr) {
    hotplug_callback_.first(hotplug_callback_.second, displayid,
                            state == DRM_MODE_CONNECTED
                                ? HWC2_CONNECTION_CONNECTED
                                : HWC2_CONNECTION_DISCONNECTED);
  }
}

void DrmHwcTwo::HandleInitialHotplugState(DrmDevice *drmDevice) {
  for (const auto &conn : drmDevice->connectors()) {
    if (conn->state() != DRM_MODE_CONNECTED)
      continue;
    HandleDisplayHotplug(conn->display(), conn->state());
  }
}

void DrmHwcTwo::HandleHotplugUEvent() {
  for (const auto &drm : resource_manager_.getDrmDevices()) {
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
      if (cur_state == DRM_MODE_CONNECTED) {
        auto &display = displays_.at(display_id);
        display.ChosePreferredConfig();
      } else {
        auto &display = displays_.at(display_id);
        display.ClearDisplay();
      }

      HandleDisplayHotplug(display_id, cur_state);
    }
  }
}

// static
int DrmHwcTwo::HookDevClose(hw_device_t * /*dev*/) {
  unsupported(__func__);
  return 0;
}

// static
void DrmHwcTwo::HookDevGetCapabilities(hwc2_device_t * /*dev*/,
                                       uint32_t *out_count,
                                       int32_t * /*out_capabilities*/) {
  supported(__func__);
  *out_count = 0;
}

// static
hwc2_function_pointer_t DrmHwcTwo::HookDevGetFunction(
    struct hwc2_device * /*dev*/, int32_t descriptor) {
  supported(__func__);
  auto func = static_cast<HWC2::FunctionDescriptor>(descriptor);
  switch (func) {
    // Device functions
    case HWC2::FunctionDescriptor::CreateVirtualDisplay:
      return ToHook<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(
          DeviceHook<int32_t, decltype(&DrmHwcTwo::CreateVirtualDisplay),
                     &DrmHwcTwo::CreateVirtualDisplay, uint32_t, uint32_t,
                     int32_t *, hwc2_display_t *>);
    case HWC2::FunctionDescriptor::DestroyVirtualDisplay:
      return ToHook<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(
          DeviceHook<int32_t, decltype(&DrmHwcTwo::DestroyVirtualDisplay),
                     &DrmHwcTwo::DestroyVirtualDisplay, hwc2_display_t>);
    case HWC2::FunctionDescriptor::Dump:
      return ToHook<HWC2_PFN_DUMP>(
          DeviceHook<void, decltype(&DrmHwcTwo::Dump), &DrmHwcTwo::Dump,
                     uint32_t *, char *>);
    case HWC2::FunctionDescriptor::GetMaxVirtualDisplayCount:
      return ToHook<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(
          DeviceHook<uint32_t, decltype(&DrmHwcTwo::GetMaxVirtualDisplayCount),
                     &DrmHwcTwo::GetMaxVirtualDisplayCount>);
    case HWC2::FunctionDescriptor::RegisterCallback:
      return ToHook<HWC2_PFN_REGISTER_CALLBACK>(
          DeviceHook<int32_t, decltype(&DrmHwcTwo::RegisterCallback),
                     &DrmHwcTwo::RegisterCallback, int32_t,
                     hwc2_callback_data_t, hwc2_function_pointer_t>);

    // Display functions
    case HWC2::FunctionDescriptor::AcceptDisplayChanges:
      return ToHook<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(
          DisplayHook<decltype(&HwcDisplay::AcceptDisplayChanges),
                      &HwcDisplay::AcceptDisplayChanges>);
    case HWC2::FunctionDescriptor::CreateLayer:
      return ToHook<HWC2_PFN_CREATE_LAYER>(
          DisplayHook<decltype(&HwcDisplay::CreateLayer),
                      &HwcDisplay::CreateLayer, hwc2_layer_t *>);
    case HWC2::FunctionDescriptor::DestroyLayer:
      return ToHook<HWC2_PFN_DESTROY_LAYER>(
          DisplayHook<decltype(&HwcDisplay::DestroyLayer),
                      &HwcDisplay::DestroyLayer, hwc2_layer_t>);
    case HWC2::FunctionDescriptor::GetActiveConfig:
      return ToHook<HWC2_PFN_GET_ACTIVE_CONFIG>(
          DisplayHook<decltype(&HwcDisplay::GetActiveConfig),
                      &HwcDisplay::GetActiveConfig, hwc2_config_t *>);
    case HWC2::FunctionDescriptor::GetChangedCompositionTypes:
      return ToHook<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(
          DisplayHook<decltype(&HwcDisplay::GetChangedCompositionTypes),
                      &HwcDisplay::GetChangedCompositionTypes, uint32_t *,
                      hwc2_layer_t *, int32_t *>);
    case HWC2::FunctionDescriptor::GetClientTargetSupport:
      return ToHook<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(
          DisplayHook<decltype(&HwcDisplay::GetClientTargetSupport),
                      &HwcDisplay::GetClientTargetSupport, uint32_t, uint32_t,
                      int32_t, int32_t>);
    case HWC2::FunctionDescriptor::GetColorModes:
      return ToHook<HWC2_PFN_GET_COLOR_MODES>(
          DisplayHook<decltype(&HwcDisplay::GetColorModes),
                      &HwcDisplay::GetColorModes, uint32_t *, int32_t *>);
    case HWC2::FunctionDescriptor::GetDisplayAttribute:
      return ToHook<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayAttribute),
                      &HwcDisplay::GetDisplayAttribute, hwc2_config_t, int32_t,
                      int32_t *>);
    case HWC2::FunctionDescriptor::GetDisplayConfigs:
      return ToHook<HWC2_PFN_GET_DISPLAY_CONFIGS>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayConfigs),
                      &HwcDisplay::GetDisplayConfigs, uint32_t *,
                      hwc2_config_t *>);
    case HWC2::FunctionDescriptor::GetDisplayName:
      return ToHook<HWC2_PFN_GET_DISPLAY_NAME>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayName),
                      &HwcDisplay::GetDisplayName, uint32_t *, char *>);
    case HWC2::FunctionDescriptor::GetDisplayRequests:
      return ToHook<HWC2_PFN_GET_DISPLAY_REQUESTS>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayRequests),
                      &HwcDisplay::GetDisplayRequests, int32_t *, uint32_t *,
                      hwc2_layer_t *, int32_t *>);
    case HWC2::FunctionDescriptor::GetDisplayType:
      return ToHook<HWC2_PFN_GET_DISPLAY_TYPE>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayType),
                      &HwcDisplay::GetDisplayType, int32_t *>);
    case HWC2::FunctionDescriptor::GetDozeSupport:
      return ToHook<HWC2_PFN_GET_DOZE_SUPPORT>(
          DisplayHook<decltype(&HwcDisplay::GetDozeSupport),
                      &HwcDisplay::GetDozeSupport, int32_t *>);
    case HWC2::FunctionDescriptor::GetHdrCapabilities:
      return ToHook<HWC2_PFN_GET_HDR_CAPABILITIES>(
          DisplayHook<decltype(&HwcDisplay::GetHdrCapabilities),
                      &HwcDisplay::GetHdrCapabilities, uint32_t *, int32_t *,
                      float *, float *, float *>);
    case HWC2::FunctionDescriptor::GetReleaseFences:
      return ToHook<HWC2_PFN_GET_RELEASE_FENCES>(
          DisplayHook<decltype(&HwcDisplay::GetReleaseFences),
                      &HwcDisplay::GetReleaseFences, uint32_t *, hwc2_layer_t *,
                      int32_t *>);
    case HWC2::FunctionDescriptor::PresentDisplay:
      return ToHook<HWC2_PFN_PRESENT_DISPLAY>(
          DisplayHook<decltype(&HwcDisplay::PresentDisplay),
                      &HwcDisplay::PresentDisplay, int32_t *>);
    case HWC2::FunctionDescriptor::SetActiveConfig:
      return ToHook<HWC2_PFN_SET_ACTIVE_CONFIG>(
          DisplayHook<decltype(&HwcDisplay::SetActiveConfig),
                      &HwcDisplay::SetActiveConfig, hwc2_config_t>);
    case HWC2::FunctionDescriptor::SetClientTarget:
      return ToHook<HWC2_PFN_SET_CLIENT_TARGET>(
          DisplayHook<decltype(&HwcDisplay::SetClientTarget),
                      &HwcDisplay::SetClientTarget, buffer_handle_t, int32_t,
                      int32_t, hwc_region_t>);
    case HWC2::FunctionDescriptor::SetColorMode:
      return ToHook<HWC2_PFN_SET_COLOR_MODE>(
          DisplayHook<decltype(&HwcDisplay::SetColorMode),
                      &HwcDisplay::SetColorMode, int32_t>);
    case HWC2::FunctionDescriptor::SetColorTransform:
      return ToHook<HWC2_PFN_SET_COLOR_TRANSFORM>(
          DisplayHook<decltype(&HwcDisplay::SetColorTransform),
                      &HwcDisplay::SetColorTransform, const float *, int32_t>);
    case HWC2::FunctionDescriptor::SetOutputBuffer:
      return ToHook<HWC2_PFN_SET_OUTPUT_BUFFER>(
          DisplayHook<decltype(&HwcDisplay::SetOutputBuffer),
                      &HwcDisplay::SetOutputBuffer, buffer_handle_t, int32_t>);
    case HWC2::FunctionDescriptor::SetPowerMode:
      return ToHook<HWC2_PFN_SET_POWER_MODE>(
          DisplayHook<decltype(&HwcDisplay::SetPowerMode),
                      &HwcDisplay::SetPowerMode, int32_t>);
    case HWC2::FunctionDescriptor::SetVsyncEnabled:
      return ToHook<HWC2_PFN_SET_VSYNC_ENABLED>(
          DisplayHook<decltype(&HwcDisplay::SetVsyncEnabled),
                      &HwcDisplay::SetVsyncEnabled, int32_t>);
    case HWC2::FunctionDescriptor::ValidateDisplay:
      return ToHook<HWC2_PFN_VALIDATE_DISPLAY>(
          DisplayHook<decltype(&HwcDisplay::ValidateDisplay),
                      &HwcDisplay::ValidateDisplay, uint32_t *, uint32_t *>);
#if PLATFORM_SDK_VERSION > 27
    case HWC2::FunctionDescriptor::GetRenderIntents:
      return ToHook<HWC2_PFN_GET_RENDER_INTENTS>(
          DisplayHook<decltype(&HwcDisplay::GetRenderIntents),
                      &HwcDisplay::GetRenderIntents, int32_t, uint32_t *,
                      int32_t *>);
    case HWC2::FunctionDescriptor::SetColorModeWithRenderIntent:
      return ToHook<HWC2_PFN_SET_COLOR_MODE_WITH_RENDER_INTENT>(
          DisplayHook<decltype(&HwcDisplay::SetColorModeWithIntent),
                      &HwcDisplay::SetColorModeWithIntent, int32_t, int32_t>);
#endif
#if PLATFORM_SDK_VERSION > 28
    case HWC2::FunctionDescriptor::GetDisplayIdentificationData:
      return ToHook<HWC2_PFN_GET_DISPLAY_IDENTIFICATION_DATA>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayIdentificationData),
                      &HwcDisplay::GetDisplayIdentificationData, uint8_t *,
                      uint32_t *, uint8_t *>);
    case HWC2::FunctionDescriptor::GetDisplayCapabilities:
      return ToHook<HWC2_PFN_GET_DISPLAY_CAPABILITIES>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayCapabilities),
                      &HwcDisplay::GetDisplayCapabilities, uint32_t *,
                      uint32_t *>);
    case HWC2::FunctionDescriptor::GetDisplayBrightnessSupport:
      return ToHook<HWC2_PFN_GET_DISPLAY_BRIGHTNESS_SUPPORT>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayBrightnessSupport),
                      &HwcDisplay::GetDisplayBrightnessSupport, bool *>);
    case HWC2::FunctionDescriptor::SetDisplayBrightness:
      return ToHook<HWC2_PFN_SET_DISPLAY_BRIGHTNESS>(
          DisplayHook<decltype(&HwcDisplay::SetDisplayBrightness),
                      &HwcDisplay::SetDisplayBrightness, float>);
#endif /* PLATFORM_SDK_VERSION > 28 */
#if PLATFORM_SDK_VERSION > 29
    case HWC2::FunctionDescriptor::GetDisplayConnectionType:
      return ToHook<HWC2_PFN_GET_DISPLAY_CONNECTION_TYPE>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayConnectionType),
                      &HwcDisplay::GetDisplayConnectionType, uint32_t *>);
    case HWC2::FunctionDescriptor::GetDisplayVsyncPeriod:
      return ToHook<HWC2_PFN_GET_DISPLAY_VSYNC_PERIOD>(
          DisplayHook<decltype(&HwcDisplay::GetDisplayVsyncPeriod),
                      &HwcDisplay::GetDisplayVsyncPeriod,
                      hwc2_vsync_period_t *>);
    case HWC2::FunctionDescriptor::SetActiveConfigWithConstraints:
      return ToHook<HWC2_PFN_SET_ACTIVE_CONFIG_WITH_CONSTRAINTS>(
          DisplayHook<decltype(&HwcDisplay::SetActiveConfigWithConstraints),
                      &HwcDisplay::SetActiveConfigWithConstraints,
                      hwc2_config_t, hwc_vsync_period_change_constraints_t *,
                      hwc_vsync_period_change_timeline_t *>);
    case HWC2::FunctionDescriptor::SetAutoLowLatencyMode:
      return ToHook<HWC2_PFN_SET_AUTO_LOW_LATENCY_MODE>(
          DisplayHook<decltype(&HwcDisplay::SetAutoLowLatencyMode),
                      &HwcDisplay::SetAutoLowLatencyMode, bool>);
    case HWC2::FunctionDescriptor::GetSupportedContentTypes:
      return ToHook<HWC2_PFN_GET_SUPPORTED_CONTENT_TYPES>(
          DisplayHook<decltype(&HwcDisplay::GetSupportedContentTypes),
                      &HwcDisplay::GetSupportedContentTypes, uint32_t *,
                      uint32_t *>);
    case HWC2::FunctionDescriptor::SetContentType:
      return ToHook<HWC2_PFN_SET_CONTENT_TYPE>(
          DisplayHook<decltype(&HwcDisplay::SetContentType),
                      &HwcDisplay::SetContentType, int32_t>);
#endif
    // Layer functions
    case HWC2::FunctionDescriptor::SetCursorPosition:
      return ToHook<HWC2_PFN_SET_CURSOR_POSITION>(
          LayerHook<decltype(&HwcLayer::SetCursorPosition),
                    &HwcLayer::SetCursorPosition, int32_t, int32_t>);
    case HWC2::FunctionDescriptor::SetLayerBlendMode:
      return ToHook<HWC2_PFN_SET_LAYER_BLEND_MODE>(
          LayerHook<decltype(&HwcLayer::SetLayerBlendMode),
                    &HwcLayer::SetLayerBlendMode, int32_t>);
    case HWC2::FunctionDescriptor::SetLayerBuffer:
      return ToHook<HWC2_PFN_SET_LAYER_BUFFER>(
          LayerHook<decltype(&HwcLayer::SetLayerBuffer),
                    &HwcLayer::SetLayerBuffer, buffer_handle_t, int32_t>);
    case HWC2::FunctionDescriptor::SetLayerColor:
      return ToHook<HWC2_PFN_SET_LAYER_COLOR>(
          LayerHook<decltype(&HwcLayer::SetLayerColor),
                    &HwcLayer::SetLayerColor, hwc_color_t>);
    case HWC2::FunctionDescriptor::SetLayerCompositionType:
      return ToHook<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(
          LayerHook<decltype(&HwcLayer::SetLayerCompositionType),
                    &HwcLayer::SetLayerCompositionType, int32_t>);
    case HWC2::FunctionDescriptor::SetLayerDataspace:
      return ToHook<HWC2_PFN_SET_LAYER_DATASPACE>(
          LayerHook<decltype(&HwcLayer::SetLayerDataspace),
                    &HwcLayer::SetLayerDataspace, int32_t>);
    case HWC2::FunctionDescriptor::SetLayerDisplayFrame:
      return ToHook<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(
          LayerHook<decltype(&HwcLayer::SetLayerDisplayFrame),
                    &HwcLayer::SetLayerDisplayFrame, hwc_rect_t>);
    case HWC2::FunctionDescriptor::SetLayerPlaneAlpha:
      return ToHook<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(
          LayerHook<decltype(&HwcLayer::SetLayerPlaneAlpha),
                    &HwcLayer::SetLayerPlaneAlpha, float>);
    case HWC2::FunctionDescriptor::SetLayerSidebandStream:
      return ToHook<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(
          LayerHook<decltype(&HwcLayer::SetLayerSidebandStream),
                    &HwcLayer::SetLayerSidebandStream,
                    const native_handle_t *>);
    case HWC2::FunctionDescriptor::SetLayerSourceCrop:
      return ToHook<HWC2_PFN_SET_LAYER_SOURCE_CROP>(
          LayerHook<decltype(&HwcLayer::SetLayerSourceCrop),
                    &HwcLayer::SetLayerSourceCrop, hwc_frect_t>);
    case HWC2::FunctionDescriptor::SetLayerSurfaceDamage:
      return ToHook<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(
          LayerHook<decltype(&HwcLayer::SetLayerSurfaceDamage),
                    &HwcLayer::SetLayerSurfaceDamage, hwc_region_t>);
    case HWC2::FunctionDescriptor::SetLayerTransform:
      return ToHook<HWC2_PFN_SET_LAYER_TRANSFORM>(
          LayerHook<decltype(&HwcLayer::SetLayerTransform),
                    &HwcLayer::SetLayerTransform, int32_t>);
    case HWC2::FunctionDescriptor::SetLayerVisibleRegion:
      return ToHook<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(
          LayerHook<decltype(&HwcLayer::SetLayerVisibleRegion),
                    &HwcLayer::SetLayerVisibleRegion, hwc_region_t>);
    case HWC2::FunctionDescriptor::SetLayerZOrder:
      return ToHook<HWC2_PFN_SET_LAYER_Z_ORDER>(
          LayerHook<decltype(&HwcLayer::SetLayerZOrder),
                    &HwcLayer::SetLayerZOrder, uint32_t>);
    case HWC2::FunctionDescriptor::Invalid:
    default:
      return nullptr;
  }
}

// static
int DrmHwcTwo::HookDevOpen(const struct hw_module_t *module, const char *name,
                           struct hw_device_t **dev) {
  supported(__func__);
  if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0) {
    ALOGE("Invalid module name- %s", name);
    return -EINVAL;
  }

  std::unique_ptr<DrmHwcTwo> ctx(new DrmHwcTwo());
  if (!ctx) {
    ALOGE("Failed to allocate DrmHwcTwo");
    return -ENOMEM;
  }

  HWC2::Error err = ctx->Init();
  if (err != HWC2::Error::None) {
    ALOGE("Failed to initialize DrmHwcTwo err=%d\n", err);
    return -EINVAL;
  }

  ctx->common.module = (hw_module_t *)module;
  *dev = &ctx->common;
  ctx.release();  // NOLINT(bugprone-unused-return-value)
  return 0;
}
}  // namespace android

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static struct hw_module_methods_t hwc2_module_methods = {
    .open = android::DrmHwcTwo::HookDevOpen,
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = HARDWARE_MODULE_API_VERSION(2, 0),
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "DrmHwcTwo module",
    .author = "The Android Open Source Project",
    .methods = &hwc2_module_methods,
    .dso = nullptr,
    .reserved = {0},
};
