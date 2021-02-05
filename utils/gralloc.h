/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef UTILS_GRALLOC_H_
#define UTILS_GRALLOC_H_

#ifdef ANDROID
#include <hardware/gralloc.h>
#else

/* STUBS */

#include <cstdint>

using buffer_handle_t = int;

// NOLINTNEXTLINE(readability-identifier-naming)
constexpr auto GRALLOC_HARDWARE_MODULE_ID = "gralloc";

enum {
  HAL_PIXEL_FORMAT_RGBA_8888 = 1,   // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_RGBX_8888 = 2,   // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_RGB_888 = 3,     // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_RGB_565 = 4,     // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_BGRA_8888 = 5,   // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_RGBA_FP16 = 22,  // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED = 34,  // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_RGBA_1010102 = 43,  // NOLINT(readability-identifier-naming)
  HAL_PIXEL_FORMAT_YV12 = 842094169,   // NOLINT(readability-identifier-naming)
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct hw_module_t {
  uint32_t tag{};
  uint16_t module_api_version{};
  uint16_t hal_api_version{};
  const char *id{};
  const char *name{};
  const char *author{};
  void *dso{};
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct gralloc_module_t {
  hw_module_t common;
};

auto inline hw_get_module(const char * /*id*/,
                          const struct hw_module_t ** /*module*/) -> int {
  return -1;
}

#endif

#endif
