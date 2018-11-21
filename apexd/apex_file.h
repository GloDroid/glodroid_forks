/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ANDROID_APEXD_APEX_FILE_H_
#define ANDROID_APEXD_APEX_FILE_H_

#include <ziparchive/zip_archive.h>
#include <memory>
#include <string>

#include "status_or.h"

namespace android {
namespace apex {

// Manages the content of an APEX package and provides utilities to navigate
// the content.
class ApexFile {
 public:
  ApexFile(ApexFile&&);

  static StatusOr<std::unique_ptr<ApexFile>> Open(const std::string& apex_path);
  ~ApexFile();

  std::string GetPath() const { return apex_path_; }
  int32_t GetImageOffset() const { return image_offset_; }
  size_t GetImageSize() const { return image_size_; }

  std::string GetManifest() const { return manifest_; }
  bool IsFlattened() const { return flattened_; }

 private:
  ApexFile(const std::string& apex_path)
      : apex_path_(apex_path), handle_(nullptr){};
  int OpenInternal(std::string* error_msg);

  const std::string apex_path_;
  bool flattened_;
  int32_t image_offset_;
  size_t image_size_;
  std::string manifest_;
  ZipArchiveHandle handle_;
};

}  // namespace apex
}  // namespace android

#endif  // ANDROID_APEXD_APEX_FILE_H_
