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

#include "apex_file.h"
#include "string_log.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ziparchive/zip_archive.h>
#include <memory>
#include <string>

namespace android {
namespace apex {
namespace {

constexpr const char* kImageFilename = "apex_payload.img";
constexpr const char* kManifestFilename = "apex_manifest.json";

// Tests if <path>/manifest.json file exists.
bool isFlattenedApex(const std::string& path) {
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

}  // namespace

StatusOr<ApexFile> ApexFile::Open(const std::string& path) {
  bool flattened;
  int32_t image_offset;
  size_t image_size;
  std::string manifest_content;

  if (isFlattenedApex(path)) {
    flattened = true;
    image_offset = 0;
    image_size = 0;
    const std::string manifest_path = path + "/" + kManifestFilename;
    if (!android::base::ReadFileToString(path, &manifest_content)) {
      std::string err = StringLog()
                        << "Failed to read manifest file: " << manifest_path;
      return StatusOr<ApexFile>::MakeError(err);
    }
  } else {
    flattened = false;

    ZipArchiveHandle handle;
    auto handle_guard =
        android::base::make_scope_guard([&handle] { CloseArchive(handle); });
    int ret = OpenArchive(path.c_str(), &handle);
    if (ret < 0) {
      std::string err = StringLog() << "Failed to open package " << path << ": "
                                    << ErrorCodeString(ret);
      return StatusOr<ApexFile>::MakeError(err);
    }

    // Locate the mountable image within the zipfile and store offset and size.
    ZipEntry entry;
    ret = FindEntry(handle, ZipString(kImageFilename), &entry);
    if (ret < 0) {
      std::string err = StringLog() << "Could not find entry \""
                                    << kImageFilename << "\" in package "
                                    << path << ": " << ErrorCodeString(ret);
      return StatusOr<ApexFile>::MakeError(err);
    }
    image_offset = entry.offset;
    image_size = entry.uncompressed_length;

    ret = FindEntry(handle, ZipString(kManifestFilename), &entry);
    if (ret < 0) {
      std::string err = StringLog() << "Could not find entry \""
                                    << kManifestFilename << "\" in package "
                                    << path << ": " << ErrorCodeString(ret);
      return StatusOr<ApexFile>::MakeError(err);
    }

    uint32_t length = entry.uncompressed_length;
    manifest_content.resize(length, '\0');
    ret = ExtractToMemory(handle, &entry,
                          reinterpret_cast<uint8_t*>(&(manifest_content)[0]),
                          length);
    if (ret != 0) {
      std::string err = StringLog()
                        << "Failed to extract manifest from package " << path
                        << ": " << ErrorCodeString(ret);
      return StatusOr<ApexFile>::MakeError(err);
    }
  }

  StatusOr<ApexManifest> manifest = ApexManifest::Parse(manifest_content);
  if (!manifest.Ok()) {
    return StatusOr<ApexFile>::MakeError(manifest.ErrorMessage());
  }

  ApexFile apexFile(path, flattened, image_offset, image_size, *manifest);
  return StatusOr<ApexFile>(std::move(apexFile));
}

}  // namespace apex
}  // namespace android
