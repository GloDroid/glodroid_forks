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

#pragma once

#include "bufferinfo/BufferInfoGetter.h"
#include "yagi/android_yagi.h"

namespace android {

class BufferInfoYagi : public BufferInfoGetter {
 public:
  ~BufferInfoYagi() override;

  auto GetBoInfo(buffer_handle_t handle) -> std::optional<BufferInfo> override;

  static std::unique_ptr<BufferInfoGetter> CreateInstance();

 private:
  BufferInfoYagi() = default;

  void *dl_handle_ = nullptr;

  static constexpr auto kYagiBiGetSymName = "yagi_bi_get";
  yagi_bi_get_t yagi_bi_get_fn_{};

  static constexpr auto kYagiInitSymName = "yagi_init";
  yagi_init_t yagi_init_fn_{};

  static constexpr auto kYagiDestroySymName = "yagi_destroy";
  yagi_destroy_t yagi_destroy_fn_{};

  struct yagi *yagi_{};
};
}  // namespace android
