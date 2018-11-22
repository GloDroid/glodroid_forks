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

#include "apex_manifest.h"
#include "string_log.h"

#include <android-base/logging.h>
#include <json/reader.h>
#include <json/value.h>
#include <memory>
#include <string>

namespace android {
namespace apex {

StatusOr<ApexManifest> ApexManifest::Parse(const std::string& content) {
  constexpr const char* kNameTag = "name";
  constexpr const char* kVersionTag = "version";
  constexpr const char* kPreInstallTag = "pre_install_hook";
  constexpr const char* kPostInstallTag = "post_install_hook";

  std::string name;
  std::string pre_install_hook;
  std::string post_install_hook;
  uint64_t version;

  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(content, root)) {
    std::string err = StringLog()
                      << "Failed to parse APEX Manifest JSON config: "
                      << reader.getFormattedErrorMessages();
    return StatusOr<ApexManifest>::MakeError(err);
  }

  std::string err_str;
  auto read_string_field = [&](const char* tag, bool req, std::string* field) {
    if (!root.isMember(tag)) {
      if (req) {
        err_str = StringLog() << "Missing required field \"" << tag
                              << "\" from APEX manifest.";
        return false;
      }
      return true;
    }

    *field = root[tag].asString();
    return true;
  };

  // name
  if (!read_string_field(kNameTag, /*req=*/true, &name)) {
    return StatusOr<ApexManifest>::MakeError(err_str);
  }

  // version
  if (!root.isMember(kVersionTag)) {
    std::string err = StringLog() << "Missing required field \"" << kVersionTag
                                  << "\" from APEX manifest.";
    return StatusOr<ApexManifest>::MakeError(err);
  }
  Json::Value jVersion = root[kVersionTag];
  if (!jVersion.isUInt64()) {
    std::string err = StringLog()
                      << "Invalid type for field \"" << kVersionTag
                      << "\" from APEX manifest, expecting integer.";
    return StatusOr<ApexManifest>::MakeError(err);
  }
  version = jVersion.asUInt64();

  // [pre|post]_install_hook
  if (!read_string_field(kPreInstallTag, /*req=*/false, &pre_install_hook)) {
    return StatusOr<ApexManifest>::MakeError(err_str);
  }
  if (!read_string_field(kPostInstallTag, /*req=*/false, &post_install_hook)) {
    return StatusOr<ApexManifest>::MakeError(err_str);
  }

  ApexManifest manifest(name, pre_install_hook, post_install_hook, version);
  return StatusOr<ApexManifest>(std::move(manifest));
}

}  // namespace apex
}  // namespace android
