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

#include "apexd_verity.h"

#include <filesystem>
#include <vector>

#include <android-base/file.h>
#include <android-base/result.h>
#include <android-base/unique_fd.h>
#include <verity/hash_tree_builder.h>

#include "apex_constants.h"
#include "apex_file.h"
#include "apexd_loop.h"
#include "apexd_utils.h"
#include "string_log.h"

namespace fs = std::filesystem;
using android::base::ErrnoError;
using android::base::Error;
using android::base::ReadFully;
using android::base::Result;
using android::base::unique_fd;

namespace android {
namespace apex {

namespace {

uint8_t HexToBin(char h) {
  if (h >= 'A' && h <= 'H') return h - 'A' + 10;
  if (h >= 'a' && h <= 'h') return h - 'a' + 10;
  return h - '0';
}

std::vector<uint8_t> HexToBin(const std::string& hex) {
  std::vector<uint8_t> bin;
  bin.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    uint8_t c = (HexToBin(hex[i]) << 4) + HexToBin(hex[i + 1]);
    bin.push_back(c);
  }
  return bin;
}

Result<void> GenerateHashTree(const ApexFile& apex,
                              const ApexVerityData& verity_data,
                              const std::string& hashtree_file) {
  unique_fd fd(TEMP_FAILURE_RETRY(open(apex.GetPath().c_str(), O_RDONLY)));
  if (fd.get() == -1) {
    return ErrnoError() << "Failed to open " << apex.GetPath();
  }

  auto block_size = verity_data.desc->hash_block_size;
  auto image_size = verity_data.desc->image_size;

  auto hash_fn = HashTreeBuilder::HashFunction(verity_data.hash_algorithm);
  if (hash_fn == nullptr) {
    return Error() << "Unsupported hash algorithm "
                   << verity_data.hash_algorithm;
  }

  auto builder = std::make_unique<HashTreeBuilder>(block_size, hash_fn);
  if (!builder->Initialize(image_size, HexToBin(verity_data.salt))) {
    return Error() << "Invalid image size " << image_size;
  }

  if (lseek(fd, apex.GetImageOffset(), SEEK_SET) == -1) {
    return ErrnoError() << "Failed to seek";
  }

  auto block_count = image_size / block_size;
  auto buf = std::vector<uint8_t>(block_size);
  while (block_count-- > 0) {
    if (!ReadFully(fd, buf.data(), block_size)) {
      return Error() << "Failed to read";
    }
    if (!builder->Update(buf.data(), block_size)) {
      return Error() << "Failed to build hashtree: Update";
    }
  }
  if (!builder->BuildHashTree()) {
    return Error() << "Failed to build hashtree: incomplete data";
  }

  auto golden_digest = HexToBin(verity_data.root_digest);
  auto digest = builder->root_hash();
  // This returns zero-padded digest.
  // resize() it to compare with golden digest,
  digest.resize(golden_digest.size());
  if (digest != golden_digest) {
    return Error() << "Failed to build hashtree: root digest mismatch";
  }

  unique_fd out_fd(TEMP_FAILURE_RETRY(
      open(hashtree_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)));
  if (!builder->WriteHashTreeToFd(out_fd, 0)) {
    return Error() << "Failed to write hashtree to " << hashtree_file;
  }
  return {};
}

}  // namespace

// Returns loop device of hashtree.
// If image payload has an embedded hash tree, then returns <empty> loop device
Result<loop::LoopbackDeviceUniqueFd> GetHashTree(
    const ApexFile& apex, const ApexVerityData& verity_data,
    const std::string& hashtree_file) {
  if (auto st = createDirIfNeeded(kApexHashTreeDir, 0700); !st) {
    return st.error();
  }

  // TODO(b/120058143): proper hashtree filename for an apex file
  // what if different apex files have same packageId(name@ver)
  // TODO(b/120058143): check if existing hastree is tampered by comparing
  // root digests. If they don't match, need to regenerate.
  auto hash_path = fs::path(hashtree_file);
  auto exists = PathExists(hash_path);
  if (!exists) {
    return exists.error();
  }
  if (!*exists) {
    if (auto st = GenerateHashTree(apex, verity_data, hash_path); !st) {
      return st.error();
    }
    LOG(INFO) << "hashtree: generated to " << hash_path;
  } else {
    LOG(INFO) << "hashtree: reuse " << hash_path;
  }

  return loop::createLoopDevice(hash_path, 0, 0);
}

void RemoveObsoleteHashTrees() {
  // TODO(b/120058143): on boot complete, remove unused hashtree files
}

}  // namespace apex
}  // namespace android
