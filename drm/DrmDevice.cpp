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

#define LOG_TAG "hwc-drm-device"

#include "DrmDevice.h"

#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <sstream>
#include <string>

#include "drm/DrmPlane.h"
#include "utils/log.h"
#include "utils/properties.h"

namespace android {

DrmDevice::DrmDevice() {
  self.reset(this);
  mDrmFbImporter = std::make_unique<DrmFbImporter>(self);
}

// NOLINTNEXTLINE (readability-function-cognitive-complexity): Fixme
std::tuple<int, int> DrmDevice::Init(const char *path, int num_displays) {
  /* TODO: Use drmOpenControl here instead */
  fd_ = UniqueFd(open(path, O_RDWR | O_CLOEXEC));
  if (fd() < 0) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Fixme
    ALOGE("Failed to open dri %s: %s", path, strerror(errno));
    return std::make_tuple(-ENODEV, 0);
  }

  int ret = drmSetClientCap(fd(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret) {
    ALOGE("Failed to set universal plane cap %d", ret);
    return std::make_tuple(ret, 0);
  }

  ret = drmSetClientCap(fd(), DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    ALOGE("Failed to set atomic cap %d", ret);
    return std::make_tuple(ret, 0);
  }

#ifdef DRM_CLIENT_CAP_WRITEBACK_CONNECTORS
  ret = drmSetClientCap(fd(), DRM_CLIENT_CAP_WRITEBACK_CONNECTORS, 1);
  if (ret) {
    ALOGI("Failed to set writeback cap %d", ret);
    ret = 0;
  }
#endif

  uint64_t cap_value = 0;
  if (drmGetCap(fd(), DRM_CAP_ADDFB2_MODIFIERS, &cap_value)) {
    ALOGW("drmGetCap failed. Fallback to no modifier support.");
    cap_value = 0;
  }
  HasAddFb2ModifiersSupport_ = cap_value != 0;

  drmSetMaster(fd());
  if (!drmIsMaster(fd())) {
    ALOGE("DRM/KMS master access required");
    return std::make_tuple(-EACCES, 0);
  }

  auto res = MakeDrmModeResUnique(fd());
  if (!res) {
    ALOGE("Failed to get DrmDevice resources");
    return std::make_tuple(-ENODEV, 0);
  }

  min_resolution_ = std::pair<uint32_t, uint32_t>(res->min_width,
                                                  res->min_height);
  max_resolution_ = std::pair<uint32_t, uint32_t>(res->max_width,
                                                  res->max_height);

  for (int i = 0; i < res->count_crtcs; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto crtc = DrmCrtc::CreateInstance(*this, res->crtcs[i], i);
    if (crtc) {
      crtcs_.emplace_back(std::move(crtc));
    }
  }

  std::vector<uint32_t> possible_clones;
  for (int i = 0; !ret && i < res->count_encoders; ++i) {
    auto e = MakeDrmModeEncoderUnique(fd(), res->encoders[i]);
    if (!e) {
      ALOGE("Failed to get encoder %d", res->encoders[i]);
      ret = -ENODEV;
      break;
    }

    std::vector<DrmCrtc *> possible_crtcs;
    DrmCrtc *current_crtc = nullptr;
    for (auto &crtc : crtcs_) {
      if ((1 << crtc->GetIndexInResArray()) & e->possible_crtcs)
        possible_crtcs.push_back(crtc.get());

      if (crtc->GetId() == e->crtc_id)
        current_crtc = crtc.get();
    }

    std::unique_ptr<DrmEncoder> enc(
        new DrmEncoder(e.get(), current_crtc, possible_crtcs));
    possible_clones.push_back(e->possible_clones);

    encoders_.emplace_back(std::move(enc));
  }

  for (unsigned int i = 0; i < encoders_.size(); i++) {
    for (unsigned int j = 0; j < encoders_.size(); j++)
      if (possible_clones[i] & (1 << j))
        encoders_[i]->AddPossibleClone(encoders_[j].get());
  }

  for (int i = 0; !ret && i < res->count_connectors; ++i) {
    auto c = MakeDrmModeConnectorUnique(fd(), res->connectors[i]);
    if (!c) {
      ALOGE("Failed to get connector %d", res->connectors[i]);
      ret = -ENODEV;
      break;
    }

    std::vector<DrmEncoder *> possible_encoders;
    DrmEncoder *current_encoder = nullptr;
    for (int j = 0; j < c->count_encoders; ++j) {
      for (auto &encoder : encoders_) {
        if (encoder->id() == c->encoders[j])
          possible_encoders.push_back(encoder.get());
        if (encoder->id() == c->encoder_id)
          current_encoder = encoder.get();
      }
    }

    std::unique_ptr<DrmConnector> conn(
        new DrmConnector(this, c.get(), current_encoder, possible_encoders));

    ret = conn->Init();
    if (ret) {
      ALOGE("Init connector %d failed", res->connectors[i]);
      break;
    }

    if (conn->writeback())
      writeback_connectors_.emplace_back(std::move(conn));
    else
      connectors_.emplace_back(std::move(conn));
  }

  auto add_displays = [this, &num_displays](bool internal, bool connected) {
    for (auto &conn : connectors_) {
      bool is_connected = conn->state() == DRM_MODE_CONNECTED;
      if ((internal ? conn->internal() : conn->external()) &&
          (connected ? is_connected : !is_connected)) {
        conn->set_display(num_displays);
        displays_[num_displays] = num_displays;
        ++num_displays;
      }
    }
  };

  /* Put internal first to ensure Primary display will be internal
   * in case at least 1 internal is available
   */
  add_displays(/*internal = */ true, /*connected = */ true);
  add_displays(/*internal = */ false, /*connected = */ true);
  add_displays(/*internal = */ true, /*connected = */ false);
  add_displays(/*internal = */ false, /*connected = */ false);

  // Catch-all for the above loops
  if (ret)
    return std::make_tuple(ret, 0);

  auto plane_res = MakeDrmModePlaneResUnique(fd());
  if (!plane_res) {
    ALOGE("Failed to get plane resources");
    return std::make_tuple(-ENOENT, 0);
  }

  for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto plane = DrmPlane::CreateInstance(*this, plane_res->planes[i]);

    if (plane) {
      planes_.emplace_back(std::move(plane));
    }
  }

  for (auto &conn : connectors_) {
    ret = CreateDisplayPipe(conn.get());
    if (ret) {
      ALOGE("Failed CreateDisplayPipe %d with %d", conn->id(), ret);
      return std::make_tuple(ret, 0);
    }
  }
  return std::make_tuple(ret, displays_.size());
}

bool DrmDevice::HandlesDisplay(int display) const {
  return displays_.find(display) != displays_.end();
}

DrmConnector *DrmDevice::GetConnectorForDisplay(int display) const {
  for (const auto &conn : connectors_) {
    if (conn->display() == display)
      return conn.get();
  }
  return nullptr;
}

DrmCrtc *DrmDevice::GetCrtcForDisplay(int display) const {
  return bound_crtcs_.at(display);
}

const std::vector<std::unique_ptr<DrmCrtc>> &DrmDevice::crtcs() const {
  return crtcs_;
}

uint32_t DrmDevice::next_mode_id() {
  return ++mode_id_;
}

int DrmDevice::TryEncoderForDisplay(int display, DrmEncoder *enc) {
  /* First try to use the currently-bound crtc */
  DrmCrtc *crtc = enc->crtc();
  if (crtc && bound_crtcs_.count(display) == 0) {
    bound_crtcs_[display] = crtc;
    enc->set_crtc(crtc, display);
    return 0;
  }

  /* Try to find a possible crtc which will work */
  for (DrmCrtc *crtc : enc->possible_crtcs()) {
    /* We've already tried this earlier */
    if (crtc == enc->crtc())
      continue;

    if (bound_crtcs_.count(display) == 0) {
      bound_crtcs_[display] = crtc;
      enc->set_crtc(crtc, display);
      return 0;
    }
  }

  /* We can't use the encoder, but nothing went wrong, try another one */
  return -EAGAIN;
}

int DrmDevice::CreateDisplayPipe(DrmConnector *connector) {
  int display = connector->display();
  /* Try to use current setup first */
  if (connector->encoder()) {
    int ret = TryEncoderForDisplay(display, connector->encoder());
    if (!ret) {
      return 0;
    }

    if (ret != -EAGAIN) {
      ALOGE("Could not set mode %d/%d", display, ret);
      return ret;
    }
  }

  for (DrmEncoder *enc : connector->possible_encoders()) {
    int ret = TryEncoderForDisplay(display, enc);
    if (!ret) {
      connector->set_encoder(enc);
      return 0;
    }

    if (ret != -EAGAIN) {
      ALOGE("Could not set mode %d/%d", display, ret);
      return ret;
    }
  }
  ALOGE("Could not find a suitable encoder/crtc for display %d",
        connector->display());
  return -ENODEV;
}

auto DrmDevice::RegisterUserPropertyBlob(void *data, size_t length) const
    -> DrmModeUserPropertyBlobUnique {
  struct drm_mode_create_blob create_blob {};
  create_blob.length = length;
  create_blob.data = (__u64)data;

  int ret = drmIoctl(fd(), DRM_IOCTL_MODE_CREATEPROPBLOB, &create_blob);
  if (ret) {
    ALOGE("Failed to create mode property blob %d", ret);
    return {};
  }

  return DrmModeUserPropertyBlobUnique(
      new uint32_t(create_blob.blob_id), [this](const uint32_t *it) {
        struct drm_mode_destroy_blob destroy_blob {};
        destroy_blob.blob_id = (__u32)*it;
        int err = drmIoctl(fd(), DRM_IOCTL_MODE_DESTROYPROPBLOB, &destroy_blob);
        if (err != 0) {
          ALOGE("Failed to destroy mode property blob %" PRIu32 "/%d", *it,
                err);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete it;
      });
}

int DrmDevice::GetProperty(uint32_t obj_id, uint32_t obj_type,
                           const char *prop_name, DrmProperty *property) const {
  drmModeObjectPropertiesPtr props = nullptr;

  props = drmModeObjectGetProperties(fd(), obj_id, obj_type);
  if (!props) {
    ALOGE("Failed to get properties for %d/%x", obj_id, obj_type);
    return -ENODEV;
  }

  bool found = false;
  for (int i = 0; !found && (size_t)i < props->count_props; ++i) {
    drmModePropertyPtr p = drmModeGetProperty(fd(), props->props[i]);
    if (!strcmp(p->name, prop_name)) {
      property->Init(obj_id, p, props->prop_values[i]);
      found = true;
    }
    drmModeFreeProperty(p);
  }

  drmModeFreeObjectProperties(props);
  return found ? 0 : -ENOENT;
}

int DrmDevice::GetConnectorProperty(const DrmConnector &connector,
                                    const char *prop_name,
                                    DrmProperty *property) const {
  return GetProperty(connector.id(), DRM_MODE_OBJECT_CONNECTOR, prop_name,
                     property);
}

std::string DrmDevice::GetName() const {
  auto *ver = drmGetVersion(fd());
  if (!ver) {
    ALOGW("Failed to get drm version for fd=%d", fd());
    return "generic";
  }

  std::string name(ver->name);
  drmFreeVersion(ver);
  return name;
}

auto DrmDevice::IsKMSDev(const char *path) -> bool {
  auto fd = UniqueFd(open(path, O_RDWR | O_CLOEXEC));
  if (!fd) {
    return false;
  }

  auto res = MakeDrmModeResUnique(fd.Get());
  if (!res) {
    return false;
  }

  bool is_kms = res->count_crtcs > 0 && res->count_connectors > 0 &&
                res->count_encoders > 0;

  return is_kms;
}

}  // namespace android
