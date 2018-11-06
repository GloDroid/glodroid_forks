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

#include "apexservice.h"

#include <fstream>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <binder/IResultReceiver.h>

#include "apexd.h"
#include "status.h"

using android::base::StringPrintf;

namespace android {
namespace apex {

using BinderStatus = ::android::binder::Status;

namespace {

BinderStatus CheckDebuggable() {
  if (!::android::base::GetBoolProperty("ro.debuggable", false)) {
    return BinderStatus::fromExceptionCode(BinderStatus::EX_SECURITY,
                                           String8("mountPackage unavailable"));
  }
  return BinderStatus::ok();
}

}  // namespace

BinderStatus ApexService::installPackage(const std::string& packageTmpPath,
                                         bool* aidl_return) {
  LOG(DEBUG) << "installPackage() received by ApexService, path "
             << packageTmpPath;

  *aidl_return = false;
  Status res = ::android::apex::installPackage(packageTmpPath);

  if (res.Ok()) {
    *aidl_return = true;
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed installing " << packageTmpPath << ": "
             << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::mountPackage(const std::string& packagePath) {
  BinderStatus debugCheck = CheckDebuggable();
  if (!debugCheck.isOk()) {
    return debugCheck;
  }

  LOG(DEBUG) << "mountPackage() received by ApexService, path " << packagePath;

  Status res = ::android::apex::mountPackage(packagePath);

  if (res.Ok()) {
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to mount " << packagePath << ": " << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::getActivePackages(
    std::vector<PackageInfo>* aidl_return) {
  LOG(DEBUG) << "Scanning " << kApexRoot
             << " looking for packages already installed.";
  // This code would be much shorter if C++17's std::filesystem were available,
  // which is not at the time of writing this.
  auto d = std::unique_ptr<DIR, int (*)(DIR*)>(opendir(kApexRoot), closedir);
  if (!d) {
    PLOG(ERROR) << "Can't open " << kApexRoot << " for reading.";
    return BinderStatus::fromExceptionCode(
        BinderStatus::EX_ILLEGAL_STATE,
        "Internal error, apex root directory is not readable or doesn't "
        "exist.");
  }

  struct dirent* dp;
  while ((dp = readdir(d.get())) != NULL) {
    if (dp->d_type != DT_DIR || (strcmp(dp->d_name, ".") == 0) ||
        (strcmp(dp->d_name, "..") == 0)) {
      continue;
    }
    PackageInfo pkg;
    std::vector<std::string> splits = android::base::Split(dp->d_name, "@");
    if (splits.size() != 2) {
      LOG(ERROR) << "Unable to extract package info from directory name "
                 << dp->d_name << "... skipping.";
      continue;
    }

    pkg.package_name = splits[0];
    pkg.version_code = atol(splits[1].c_str());
    aidl_return->push_back(pkg);
  }
  return BinderStatus::ok();
}

status_t ApexService::onTransact(uint32_t _aidl_code, const Parcel& _aidl_data,
                                 Parcel* _aidl_reply, uint32_t _aidl_flags) {
  switch (_aidl_code) {
    case IBinder::SHELL_COMMAND_TRANSACTION: {
      int in = _aidl_data.readFileDescriptor();
      int out = _aidl_data.readFileDescriptor();
      int err = _aidl_data.readFileDescriptor();
      int argc = _aidl_data.readInt32();
      Vector<String16> args;
      for (int i = 0; i < argc && _aidl_data.dataAvail() > 0; i++) {
        args.add(_aidl_data.readString16());
      }
      sp<IBinder> unusedCallback;
      sp<IResultReceiver> resultReceiver;
      status_t status;
      if ((status = _aidl_data.readNullableStrongBinder(&unusedCallback)) != OK)
        return status;
      if ((status = _aidl_data.readNullableStrongBinder(&resultReceiver)) != OK)
        return status;
      status = shellCommand(in, out, err, args);
      if (resultReceiver != nullptr) {
        resultReceiver->send(status);
      }
      return OK;
    }
  }
  return BnApexService::onTransact(_aidl_code, _aidl_data, _aidl_reply,
                                   _aidl_flags);
}

status_t ApexService::shellCommand(int in, int out, int err,
                                   const Vector<String16>& args) {
  if (in == BAD_TYPE || out == BAD_TYPE || err == BAD_TYPE) {
    return BAD_VALUE;
  }
  auto print_help = [](int fd) {
    auto stream = std::fstream(base::StringPrintf("/proc/self/fd/%d", fd));
    stream
        << "ApexService:" << std::endl
        << "  help - display this help" << std::endl
        << "  installPackage [packagePath] - install package from the given "
           "path"
        << std::endl
        << "  getActivePackages - return the list of active packages"
        << std::endl
        << "  mountPackage [packagePath]   - mount package from the given path"
        << std::endl;
  };

  if (args.size() == 2 && args[0] == String16("installPackage")) {
    bool ret_value;
    ::android::binder::Status status =
        installPackage(String8(args[1]).string(), &ret_value);
    if (status.isOk()) {
      return OK;
    }
    auto err_str = std::fstream(base::StringPrintf("/proc/self/fd/%d", err));
    err_str << "Failed to install package: " << status.toString8().string()
            << std::endl;
    return BAD_VALUE;
  }
  if (args.size() == 1 && args[0] == String16("getActivePackages")) {
    std::vector<PackageInfo> list;
    android::binder::Status status = getActivePackages(&list);
    if (status.isOk()) {
      auto out_str = std::fstream(base::StringPrintf("/proc/self/fd/%d", out));
      for (auto item : list) {
        out_str << "Package: " << item.package_name
                << " Version: " << item.version_code << std::endl;
      }
      return OK;
    }
    auto err_str = std::fstream(base::StringPrintf("/proc/self/fd/%d", err));
    err_str << "Failed to retrieve packages: " << status.toString8().string()
            << std::endl;
    return BAD_VALUE;
  }
  if (args.size() == 2 && args[0] == String16("mountPackage")) {
    ::android::binder::Status status = mountPackage(String8(args[1]).string());
    if (status.isOk()) {
      return OK;
    }
    auto err_str = std::fstream(base::StringPrintf("/proc/self/fd/%d", err));
    err_str << "Failed to mount package: " << status.toString8().string()
            << std::endl;
    return BAD_VALUE;
  }
  if (args.size() == 1 && args[0] == String16("help")) {
    print_help(out);
    return OK;
  }
  print_help(err);
  return BAD_VALUE;
}

}  // namespace apex
}  // namespace android
