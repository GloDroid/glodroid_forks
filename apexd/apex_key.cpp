/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "apexd"

#include "apex_key.h"

#include <unordered_map>

#include <android-base/file.h>
#include <android-base/result.h>
#include <android-base/strings.h>

#include "apex_constants.h"
#include "apex_file.h"
#include "apexd_utils.h"
#include "string_log.h"

using android::base::Error;
using android::base::Result;

namespace android {
namespace apex {

namespace {

std::unordered_map<std::string, const std::string> gScannedApexKeys;

using KeyPair = std::pair<std::string, std::string>;
Result<std::vector<KeyPair>> collectEmbedddedApexKeysFromDir(
    const std::string& dir) {
  LOG(INFO) << "Scanning " << dir << " for embedded keys";
  // list of <key_name, key_content> pairs
  std::vector<KeyPair> ret;
  if (access(dir.c_str(), F_OK) != 0 && errno == ENOENT) {
    LOG(INFO) << "... does not exist. Skipping";
    return ret;
  }
  const bool scanBuiltinApexes = isPathForBuiltinApexes(dir);
  if (!scanBuiltinApexes) {
    return Error() << "Can't scan embedded APEX keys from " << dir;
  }
  Result<std::vector<std::string>> apex_files = FindApexFilesByName(dir);
  if (!apex_files) {
    return apex_files.error();
  }

  for (const auto& file : *apex_files) {
    Result<ApexFile> apex_file = ApexFile::Open(file);
    if (!apex_file) {
      return Error() << "Failed to open " << file << " : " << apex_file.error();
    }
    // name of the key is the name of the apex that the key is bundled in
    ret.push_back(std::make_pair(apex_file->GetManifest().name(),
                                 apex_file->GetBundledPublicKey()));
  }
  return ret;
}

Result<void> updateScannedApexKeys(const std::vector<KeyPair>& key_pairs) {
  for (const KeyPair& kp : key_pairs) {
    if (gScannedApexKeys.find(kp.first) == gScannedApexKeys.end()) {
      gScannedApexKeys.insert({kp.first, kp.second});
    } else {
      const std::string& existing_key = gScannedApexKeys.at(kp.first);
      if (existing_key != kp.second) {
        return Error() << "Key for package " << kp.first
                       << " does not match with the existing key";
      }
    }
  }
  return {};
}

}  // namespace

Result<void> collectApexKeys(const std::vector<std::string>& dirs) {
  for (const auto& dir : dirs) {
    Result<std::vector<KeyPair>> key_pairs =
        collectEmbedddedApexKeysFromDir(dir);
    if (!key_pairs) {
      return Error() << "Failed to collect keys from " << dir << " : "
                     << key_pairs.error();
    }
    Result<void> st = updateScannedApexKeys(*key_pairs);
    if (!st) {
      return st;
    }
  }
  return {};
}

Result<const std::string> getApexKey(const std::string& key_name) {
  if (gScannedApexKeys.find(key_name) == gScannedApexKeys.end()) {
    return Error() << "No key found for package " << key_name;
  }
  return gScannedApexKeys[key_name];
}

}  // namespace apex
}  // namespace android
