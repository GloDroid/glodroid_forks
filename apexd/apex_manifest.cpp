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

#include <android-base/logging.h>
#include <memory>
#include <string>

#include <json/reader.h>
#include <json/value.h>

#include "apex_manifest.h"

namespace android {
namespace apex {

std::unique_ptr<ApexManifest> ApexManifest::Open(
    const std::string& apex_manifest) {
  std::unique_ptr<ApexManifest> ret(new ApexManifest(apex_manifest));
  if (ret->OpenInternal() < 0) {
    return nullptr;
  }
  return ret;
}

int ApexManifest::OpenInternal() {
  Json::Value root;
  Json::Reader reader;

  if (!reader.parse(manifest_, root)) {
    LOG(ERROR) << "Failed to parse APEX Manifest JSON config: "
               << reader.getFormattedErrorMessages();
    return -1;
  }

  if (!root.isMember("name")) {
    LOG(ERROR) << "Missing required field \"name\" from APEX manifest.";
    return -1;
  }
  Json::Value name = root["name"];
  name_ = name.asString();

  if (!root.isMember("version")) {
    LOG(ERROR) << "Missing required field \"version\" from APEX manifest.";
    return -1;
  }
  Json::Value version = root["version"];
  if (!version.isUInt64()) {
    LOG(ERROR) << "Invalid type for field \"version\" from APEX manifest, "
                  "expecting integer.";
    return -1;
  }
  version_ = version.asUInt64();

  return 0;
}

}  // namespace apex
}  // namespace android
