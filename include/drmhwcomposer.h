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

#ifndef ANDROID_DRM_HWCOMPOSER_H_
#define ANDROID_DRM_HWCOMPOSER_H_

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <stdbool.h>
#include <stdint.h>

#include <vector>

#include "autofd.h"
#include "drm/DrmFbImporter.h"
#include "drmhwcgralloc.h"

class DrmFbIdHandle;
struct hwc_import_context;

int hwc_import_init(struct hwc_import_context **ctx);
int hwc_import_destroy(struct hwc_import_context *ctx);

int hwc_import_bo_create(int fd, struct hwc_import_context *ctx,
                         buffer_handle_t buf, struct hwc_drm_bo *bo);
bool hwc_import_bo_release(int fd, struct hwc_import_context *ctx,
                           struct hwc_drm_bo *bo);

namespace android {

class DrmHwcBuffer {
 public:
  DrmHwcBuffer() = default;
  DrmHwcBuffer(const hwc_drm_bo &bo, DrmDevice *drmDevice)
      : bo_(bo), mDrmDevice(drmDevice) {
  }
  DrmHwcBuffer(DrmHwcBuffer &&rhs) : bo_(rhs.bo_), mDrmDevice(rhs.mDrmDevice) {
    rhs.mDrmDevice = nullptr;
    FbIdHandle.swap(rhs.FbIdHandle);
  }

  ~DrmHwcBuffer() {
  }

  DrmHwcBuffer &operator=(DrmHwcBuffer &&rhs) {
    FbIdHandle.swap(rhs.FbIdHandle);
    mDrmDevice = rhs.mDrmDevice;
    rhs.mDrmDevice = nullptr;
    bo_ = rhs.bo_;
    return *this;
  }

  operator bool() const {
    return mDrmDevice != NULL;
  }

  const hwc_drm_bo *operator->() const;

  void Clear();

  std::shared_ptr<DrmFbIdHandle> FbIdHandle;

  int ImportBuffer(buffer_handle_t handle, DrmDevice *drmDevice);

 private:
  hwc_drm_bo bo_;
  DrmDevice *mDrmDevice;
};

class DrmHwcNativeHandle {
 public:
  DrmHwcNativeHandle() = default;

  DrmHwcNativeHandle(native_handle_t *handle) : handle_(handle) {
  }

  DrmHwcNativeHandle(DrmHwcNativeHandle &&rhs) {
    handle_ = rhs.handle_;
    rhs.handle_ = NULL;
  }

  ~DrmHwcNativeHandle();

  DrmHwcNativeHandle &operator=(DrmHwcNativeHandle &&rhs) {
    Clear();
    handle_ = rhs.handle_;
    rhs.handle_ = NULL;
    return *this;
  }

  int CopyBufferHandle(buffer_handle_t handle);

  void Clear();

  buffer_handle_t get() const {
    return handle_;
  }

 private:
  native_handle_t *handle_ = NULL;
};

enum DrmHwcTransform {
  kIdentity = 0,
  kFlipH = 1 << 0,
  kFlipV = 1 << 1,
  kRotate90 = 1 << 2,
  kRotate180 = 1 << 3,
  kRotate270 = 1 << 4,
};

enum class DrmHwcBlending : int32_t {
  kNone = HWC_BLENDING_NONE,
  kPreMult = HWC_BLENDING_PREMULT,
  kCoverage = HWC_BLENDING_COVERAGE,
};

struct DrmHwcLayer {
  buffer_handle_t sf_handle = NULL;
  int gralloc_buffer_usage = 0;
  DrmHwcBuffer buffer;
  DrmHwcNativeHandle handle;
  uint32_t transform;
  DrmHwcBlending blending = DrmHwcBlending::kNone;
  uint16_t alpha = 0xffff;
  hwc_frect_t source_crop;
  hwc_rect_t display_frame;
  android_dataspace_t dataspace;

  UniqueFd acquire_fence;
  OutputFd release_fence;

  int ImportBuffer(DrmDevice *drmDevice);
  int InitFromDrmHwcLayer(DrmHwcLayer *layer, DrmDevice *drmDevice);

  void SetTransform(int32_t sf_transform);

  buffer_handle_t get_usable_handle() const {
    return handle.get() != NULL ? handle.get() : sf_handle;
  }

  bool protected_usage() const {
    return (gralloc_buffer_usage & GRALLOC_USAGE_PROTECTED) ==
           GRALLOC_USAGE_PROTECTED;
  }
};

}  // namespace android

#endif
