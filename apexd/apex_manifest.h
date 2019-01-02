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

#include "status_or.h"

#include <string>

namespace android {
namespace apex {

// Parses an APEX manifest on construction and exposes its fields.
class ApexManifest {
 public:
  static StatusOr<ApexManifest> Parse(const std::string& content);
  ApexManifest() = delete;
  ApexManifest(ApexManifest&&) = default;

  const std::string& GetName() const { return name_; }
  uint64_t GetVersion() const { return version_; }
  const std::string& GetPackageId() const { return package_id_; }
  const std::string& GetPreInstallHook() const { return preInstallHook_; }
  const std::string& GetPostInstallHook() const { return postInstallHook_; }

 private:
  ApexManifest(std::string& name, std::string& preInstallHook,
               std::string& postInstallHook, uint64_t version)
      : name_(std::move(name)),
        preInstallHook_(std::move(preInstallHook)),
        postInstallHook_(std::move(postInstallHook)),
        version_(version),
        package_id_(name_ + "@" + std::to_string(version_)) {}

  std::string name_;
  std::string preInstallHook_;
  std::string postInstallHook_;
  uint64_t version_;
  std::string package_id_;
};

}  // namespace apex
}  // namespace android

#endif  // ANDROID_APEXD_APEX_MANIFEST_H_
