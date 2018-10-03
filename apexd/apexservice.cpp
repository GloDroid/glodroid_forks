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
#include <android-base/stringprintf.h>

#include <stdio.h>


#include "apex_file.h"
#include "apex_manifest.h"

#include "apexservice.h"

using android::base::StringPrintf;

namespace android {
namespace apex {

static constexpr const char* kApexPackageDir = "/data/apex";
static constexpr const char* kApexPackageSuffix = ".apex";

::android::binder::Status ApexService::installPackage(const std::string& packageTmpPath, bool* aidl_return) {
  *aidl_return = true;

  LOG(DEBUG) << "installPackage() received by ApexService, path " << packageTmpPath;

  std::unique_ptr<ApexFile> apex = ApexFile::Open(packageTmpPath);
  if (apex.get() == nullptr) {
    *aidl_return = false;
    // Error opening the file.
    return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
  }

  std::unique_ptr<ApexManifest> manifest =
    ApexManifest::Open(apex->GetManifest());
  if (manifest.get() == nullptr) {
    // Error parsing manifest.
    return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
  }
  std::string packageId =
      manifest->GetName() + "@" + std::to_string(manifest->GetVersion());

  std::string destPath = StringPrintf("%s/%s%s", kApexPackageDir, packageId.c_str(), kApexPackageSuffix);
  if (rename(packageTmpPath.c_str(), destPath.c_str()) != 0) {
    PLOG(WARNING) << "Unable to rename " << packageTmpPath << " to " << destPath;
    return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_STATE);
  }
  LOG(DEBUG) << "Success renaming " << packageTmpPath << " to " << destPath;
  return binder::Status::ok();
}

};  // namespace apex
};  // namespace android
