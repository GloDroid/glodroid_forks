/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "hwc-bufferinfo-yagi"

#include "BufferInfoYagi.h"

#include <dlfcn.h>

#include <cerrno>

#include "utils/log.h"
#include "utils/properties.h"

namespace android {

std::unique_ptr<BufferInfoGetter> BufferInfoYagi::CreateInstance() {
  char lib_name[PROPERTY_VALUE_MAX];
  property_get("vendor.hwc.drm.yagi.lib", lib_name, "");
  if (strlen(lib_name) == 0) {
    return {};
  }

  ALOGI("Using YAGI library %s", lib_name);

  auto big = std::unique_ptr<BufferInfoYagi>(new BufferInfoYagi());

  big->dl_handle_ = dlopen(lib_name, RTLD_NOW);

  if (big->dl_handle_ == nullptr) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): We aren't using multithread here
    ALOGE("Failed to dlopen '%s' library: %s", lib_name, dlerror());
    return {};
  }

  big->yagi_bi_get_fn_ = yagi_bi_get_t(
      dlsym(big->dl_handle_, kYagiBiGetSymName));

  if (big->yagi_bi_get_fn_ == nullptr) {
    ALOGE("Failed get symbol %s", kYagiBiGetSymName);
    return {};
  }

  big->yagi_init_fn_ = yagi_init_t(dlsym(big->dl_handle_, kYagiInitSymName));

  if (big->yagi_init_fn_ == nullptr) {
    ALOGE("Failed get symbol %s", kYagiInitSymName);
    return {};
  }

  big->yagi_destroy_fn_ = yagi_destroy_t(
      dlsym(big->dl_handle_, kYagiDestroySymName));

  if (big->yagi_destroy_fn_ == nullptr) {
    ALOGE("Failed get symbol %s", kYagiDestroySymName);
    return {};
  }

  int yagi_api_version = 0;

  big->yagi_ = big->yagi_init_fn_(&yagi_api_version);

  if (big->yagi_ == nullptr || yagi_api_version < 1) {
    ALOGE("Failed to init YAGI");
    return {};
  }

  ALOGI("YAGI initialized, API version: %i", yagi_api_version);

  return big;
}

auto BufferInfoYagi::GetBoInfo(buffer_handle_t handle)
    -> std::optional<BufferInfo> {
  if (handle == nullptr) {
    return {};
  }

  struct yagi_bi_v1 ybi = {};

  auto ret = yagi_bi_get_fn_(yagi_, handle, &ybi, 1, sizeof(ybi));
  if (ret != 0) {
    /* Some YAGI may report only HWFB buffers and return -EAGAIN for other,
     * which is a signal to HWC to compose layer using CLIENT. We should not
     * print any error in this case.
     */
    if (ret != -EAGAIN) {
      ALOGE("YAGI: Failed to get buffer info");
    }
    return {};
  }

  BufferInfo bi{};

  bi.width = ybi.width;
  bi.height = ybi.height;
  bi.format = ybi.drm_format;

  for (int i = 0; i < ybi.num_planes; i++) {
    bi.pitches[i] = ybi.pitches[i];
    bi.offsets[i] = ybi.offsets[i];
    bi.modifiers[i] = ybi.modifiers[i];
    bi.prime_fds[i] = ybi.prime_fds[i];
  }

  return bi;
}

BufferInfoYagi::~BufferInfoYagi() {
  if (yagi_destroy_fn_ != nullptr && yagi_ != nullptr) {
    yagi_destroy_fn_(yagi_);
  }

  if (dl_handle_ != nullptr) {
    dlclose(dl_handle_);
  }
}

}  // namespace android
