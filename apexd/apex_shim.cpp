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

#include "apex_shim.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "apex_file.h"
#include "status.h"
#include "status_or.h"
#include "string_log.h"

namespace android {
namespace apex {
namespace shim {

namespace {

static constexpr const char* kApexCtsShimPackage = "com.android.cts.shim";
static constexpr const char* kHashFileName = "hash.txt";
static constexpr const int kBufSize = 1024;

Status ValidateImage(const std::string& path) {
  // TODO(b/128625955): validate that image contains only hash.txt
  return Status::Success();
}

StatusOr<std::string> CalculateSha512(const std::string& path) {
  using StatusT = StatusOr<std::string>;
  LOG(DEBUG) << "Calculating SHA512 of " << path;
  SHA512_CTX ctx;
  SHA512_Init(&ctx);
  std::ifstream apex(path, std::ios::binary);
  if (apex.bad()) {
    return StatusT::MakeError(StringLog() << "Failed to open " << path);
  }
  char buf[kBufSize];
  while (!apex.eof()) {
    apex.read(buf, kBufSize);
    if (apex.bad()) {
      return StatusT::MakeError(StringLog() << "Failed to read " << path);
    }
    int bytes_read = apex.gcount();
    SHA512_Update(&ctx, buf, bytes_read);
  }
  uint8_t hash[SHA512_DIGEST_LENGTH];
  SHA512_Final(hash, &ctx);
  std::stringstream ss;
  ss << std::hex;
  for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
    ss << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
  }
  return StatusT(ss.str());
}

StatusOr<std::string> ReadSha512(const std::string& path) {
  using StatusT = StatusOr<std::string>;
  std::string file_path =
      android::base::StringPrintf("%s/%s", path.c_str(), kHashFileName);
  LOG(DEBUG) << "Reading SHA512 from " << file_path;
  std::string hash;
  if (!android::base::ReadFileToString(file_path, &hash,
                                       false /* follows symlinks */)) {
    return StatusT::MakeError(PStringLog() << "Failed to read " << file_path);
  }
  return StatusT(android::base::Trim(hash));
}

}  // namespace

bool IsShimApex(const ApexFile& apex_file) {
  return apex_file.GetManifest().name() == kApexCtsShimPackage;
}

Status ValidateShimApex(const ApexFile& apex_file) {
  LOG(DEBUG) << "Validating shim apex " << apex_file.GetPath();
  return ValidateImage(apex_file.GetPath());
}

Status ValidateUpdate(const std::string& old_apex_path,
                      const std::string& new_apex_path) {
  LOG(DEBUG) << "Validating update of shim apex from " << old_apex_path
             << " to " << new_apex_path;
  auto expected_hash = ReadSha512(old_apex_path);
  if (!expected_hash.Ok()) {
    return expected_hash.ErrorStatus();
  }
  auto actual_hash = CalculateSha512(new_apex_path);
  if (!actual_hash.Ok()) {
    return actual_hash.ErrorStatus();
  }
  if (*actual_hash != *expected_hash) {
    return Status::Fail(StringLog()
                        << new_apex_path << " has unexpected SHA512 hash "
                        << *actual_hash);
  }
  return Status::Success();
}

}  // namespace shim
}  // namespace apex
}  // namespace android
