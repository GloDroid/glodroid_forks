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

#define LOG_TAG "apexd"

#include "apexd.h"

#include <android-base/logging.h>

#include "apexservice.h"

int main(int /*argc*/, char** argv) {
  android::base::InitLogging(argv);

  android::apex::onStart();

  // TODO: add a -v flag or an external setting to change LogSeverity.
  android::base::SetMinimumLogSeverity(android::base::VERBOSE);

  android::apex::binder::CreateAndRegisterService();

  android::apex::unmountAndDetachExistingImages();
  // Scan the directory under /data first, as it may contain updates of APEX
  // packages living in the directory under /system, and we want the former ones
  // to be used over the latter ones.
  android::apex::scanPackagesDirAndActivate(android::apex::kApexPackageDataDir);
  android::apex::scanPackagesDirAndActivate(
      android::apex::kApexPackageSystemDir);

  // Notify other components (e.g. init) that all APEXs are correctly mounted
  // and are ready to be used.
  android::apex::onAllPackagesReady();

  android::apex::binder::JoinThreadPool();

  return 1;
}
