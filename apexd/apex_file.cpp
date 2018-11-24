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

#include <android-base/file.h>
#include <android-base/logging.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ziparchive/zip_archive.h>
#include <memory>
#include <string>

#include "apex_file.h"
#include "string_log.h"

namespace android {
namespace apex {

ApexFile::ApexFile(ApexFile&& other)
    : apex_path_(other.apex_path_),
      flattened_(other.flattened_),
      image_offset_(other.image_offset_),
      image_size_(other.image_size_),
      manifest_(other.manifest_),
      handle_(other.handle_) {
  other.handle_ = nullptr;
}

StatusOr<std::unique_ptr<ApexFile>> ApexFile::Open(
    const std::string& apex_path) {
  std::unique_ptr<ApexFile> ret(new ApexFile(apex_path));
  std::string error_msg;
  if (ret->OpenInternal(&error_msg) < 0) {
    return StatusOr<std::unique_ptr<ApexFile>>::MakeError(error_msg);
  }
  return StatusOr<std::unique_ptr<ApexFile>>(std::move(ret));
}

ApexFile::~ApexFile() {
  if (handle_ != nullptr) {
    CloseArchive(handle_);
  }
}

static constexpr const char* kImageFilename = "apex_payload.img";
static constexpr const char* kManifestFilename = "apex_manifest.json";

// Tests if <path>/apex_manifest.json file exists.
static bool isFlattenedApex(const std::string& path) {
  struct stat buf;
  const std::string manifest = path + "/" + kManifestFilename;
  if (stat(manifest.c_str(), &buf) != 0) {
    if (errno == ENOENT) {
      return false;
    }
    // If the APEX is there but not a flatttened apex, the final component
    // of path will be a file, and stat will complain that it's not a directory.
    // We are OK with that to avoid two stat calls.
    if (errno != ENOTDIR) {
      PLOG(ERROR) << "Failed to stat " << path;
    }
    return false;
  }

  if (!S_ISREG(buf.st_mode)) {
    return false;
  }

  return true;
}

int ApexFile::OpenInternal(std::string* error_msg) {
  if (handle_ != nullptr) {
    // Already opened.
    return 0;
  }

  if (isFlattenedApex(apex_path_)) {
    image_offset_ = 0;
    image_size_ = 0;
    const std::string manifest_path = apex_path_ + "/" + kManifestFilename;
    if (!android::base::ReadFileToString(manifest_path, &manifest_)) {
      *error_msg = StringLog()
                   << "Failed to read manifest file: " << manifest_path;
      return -1;
    }
    flattened_ = true;
    return 0;
  }

  flattened_ = false;

  int ret = OpenArchive(apex_path_.c_str(), &handle_);
  if (ret < 0) {
    *error_msg = StringLog() << "Failed to open package " << apex_path_ << ": "
                             << ErrorCodeString(ret);
    return ret;
  }

  // Locate the mountable image within the zipfile and store offset and size.
  ZipEntry entry;
  ret = FindEntry(handle_, ZipString(kImageFilename), &entry);
  if (ret < 0) {
    *error_msg = StringLog() << "Could not find entry \"" << kImageFilename
                             << "\" in package " << apex_path_ << ": "
                             << ErrorCodeString(ret);
    return ret;
  }
  image_offset_ = entry.offset;
  image_size_ = entry.uncompressed_length;

  ret = FindEntry(handle_, ZipString(kManifestFilename), &entry);
  if (ret < 0) {
    *error_msg = StringLog() << "Could not find entry \"" << kManifestFilename
                             << "\" in package " << apex_path_ << ": "
                             << ErrorCodeString(ret);
    return ret;
  }

  uint32_t length = entry.uncompressed_length;
  manifest_.resize(length, '\0');
  ret = ExtractToMemory(handle_, &entry,
                        reinterpret_cast<uint8_t*>(&(manifest_)[0]), length);
  if (ret != 0) {
    *error_msg = StringLog() << "Failed to extract manifest from package "
                             << apex_path_ << ": " << ErrorCodeString(ret);
    return ret;
  }
  return 0;
}

}  // namespace apex
}  // namespace android
