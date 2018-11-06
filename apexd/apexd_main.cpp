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
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>

#include "apexservice.h"

using android::defaultServiceManager;
using android::IPCThreadState;
using android::ProcessState;
using android::sp;
using android::String16;
using android::apex::ApexService;

static constexpr const char* kApexPackageSystemDir = "/system/apex";
static constexpr const char* kApexServiceName = "apexservice";

int main(int /*argc*/, char** argv) {
  android::base::InitLogging(argv);

  android::apex::onStart();

  sp<ProcessState> ps(ProcessState::self());

  // TODO: add a -v flag or an external setting to change LogSeverity.
  android::base::SetMinimumLogSeverity(android::base::VERBOSE);

  // Create binder service and register with servicemanager
  sp<ApexService> apexService = new ApexService();
  defaultServiceManager()->addService(String16(kApexServiceName), apexService);

  android::apex::unmountAndDetachExistingImages();
  // Scan the directory under /data first, as it may contain updates of APEX
  // packages living in the directory under /system, and we want the former ones
  // to be used over the latter ones.
  android::apex::scanPackagesDirAndActivate(android::apex::kApexPackageDataDir);
  android::apex::scanPackagesDirAndActivate(kApexPackageSystemDir);

  // Notify other components (e.g. init) that all APEXs are correctly mounted and
  // are ready to be used.
  android::apex::onAllPackagesReady();

  // Start threadpool, wait for IPC
  ps->startThreadPool();
  IPCThreadState::self()->joinThreadPool();  // should not return

  return 1;
}
