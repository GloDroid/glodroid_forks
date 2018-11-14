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

#ifndef ANDROID_APEXD_APEX_MANIFEST_H_
#define ANDROID_APEXD_APEX_MANIFEST_H_

#include <string>

#include <status_or.h>

namespace android {
namespace apex {

// Parses an APEX manifest on construction and exposes its fields.
class ApexManifest {
 public:
  static StatusOr<std::unique_ptr<ApexManifest>> Open(
      const std::string& apex_manifest);

  std::string GetName() const { return name_; }
  uint64_t GetVersion() const { return version_; }
  std::string GetPackageId() const {
    return name_ + "@" + std::to_string(version_);
  }

 private:
  ApexManifest(const std::string& apex_manifest) : manifest_(apex_manifest){};
  int OpenInternal(std::string* error_msg);

  std::string manifest_;
  std::string name_;
  uint64_t version_;
};

}  // namespace apex
}  // namespace android

#endif  // ANDROID_APEXD_APEX_MANIFEST_H_
