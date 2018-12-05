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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <binder/IPCThreadState.h>
#include <binder/IResultReceiver.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>

#include "apexd.h"
#include "status.h"
#include "string_log.h"

#include <android/apex/BnApexService.h>

using android::base::StringPrintf;

namespace android {
namespace apex {
namespace binder {
namespace {

using BinderStatus = ::android::binder::Status;

class ApexService : public BnApexService {
 public:
  using BinderStatus = ::android::binder::Status;

  ApexService(){};

  BinderStatus stagePackage(const std::string& packageTmpPath,
                            bool* aidl_return) override;
  BinderStatus activatePackage(const std::string& packagePath) override;
  BinderStatus deactivatePackage(const std::string& packagePath) override;
  BinderStatus getActivePackages(std::vector<ApexInfo>* aidl_return) override;

  // Override onTransact so we can handle shellCommand.
  status_t onTransact(uint32_t _aidl_code, const Parcel& _aidl_data,
                      Parcel* _aidl_reply, uint32_t _aidl_flags = 0) override;

  status_t shellCommand(int in, int out, int err, const Vector<String16>& args);
};

BinderStatus CheckDebuggable(const std::string& name) {
  if (!::android::base::GetBoolProperty("ro.debuggable", false)) {
    std::string tmp = name + " unavailable";
    return BinderStatus::fromExceptionCode(BinderStatus::EX_SECURITY,
                                           String8(tmp.c_str()));
  }
  return BinderStatus::ok();
}

BinderStatus ApexService::stagePackage(const std::string& packageTmpPath,
                                       bool* aidl_return) {
  LOG(DEBUG) << "stagePackage() received by ApexService, path "
             << packageTmpPath;

  *aidl_return = false;
  Status res = ::android::apex::stagePackage(packageTmpPath);

  if (res.Ok()) {
    *aidl_return = true;
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to stage " << packageTmpPath << ": "
             << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::activatePackage(const std::string& packagePath) {
  BinderStatus debugCheck = CheckDebuggable("activatePackage");
  if (!debugCheck.isOk()) {
    return debugCheck;
  }

  LOG(DEBUG) << "activatePackage() received by ApexService, path "
             << packagePath;

  Status res = ::android::apex::activatePackage(packagePath);

  if (res.Ok()) {
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to activate " << packagePath << ": "
             << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::deactivatePackage(const std::string& packagePath) {
  BinderStatus debugCheck = CheckDebuggable("deactivatePackage");
  if (!debugCheck.isOk()) {
    return debugCheck;
  }

  LOG(DEBUG) << "deactivatePackage() received by ApexService, path "
             << packagePath;

  Status res = ::android::apex::deactivatePackage(packagePath);

  if (res.Ok()) {
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to deactivate " << packagePath << ": "
             << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::getActivePackages(
    std::vector<ApexInfo>* aidl_return) {
  auto data = ::android::apex::getActivePackages();
  for (const auto& pair : data) {
    ApexInfo pkg;
    pkg.packageName = pair.first;
    pkg.versionCode = pair.second;
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
    std::string msg = StringLog()
           << "ApexService:" << std::endl
           << "  help - display this help" << std::endl
           << "  stagePackage [packagePath] - stage package from the given "
              "path"
           << std::endl
           << "  getActivePackages - return the list of active packages"
           << std::endl
           << "  activatePackage [packagePath] - activate package from the "
              "given path"
           << std::endl
           << "  deactivatePackage [packagePath] - deactivate package from the "
              "given path"
           << std::endl;
    dprintf(fd, "%s", msg.c_str());
  };

  if (args.size() == 2 && args[0] == String16("stagePackage")) {
    bool ret_value;
    ::android::binder::Status status =
        stagePackage(String8(args[1]).string(), &ret_value);
    if (status.isOk()) {
      return OK;
    }
    std::string msg = StringLog()
            << "Failed to stage package: " << status.toString8().string()
            << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }
  if (args.size() == 1 && args[0] == String16("getActivePackages")) {
    std::vector<ApexInfo> list;
    android::binder::Status status = getActivePackages(&list);
    if (status.isOk()) {
      for (auto item : list) {
        std::string msg = StringLog()
                << "Package: " << item.packageName
                << " Version: " << item.versionCode << std::endl;
        dprintf(out, "%s", msg.c_str());
      }
      return OK;
    }
    std::string msg = StringLog()
            << "Failed to retrieve packages: " << status.toString8().string()
            << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }
  if (args.size() == 2 && args[0] == String16("activatePackage")) {
    ::android::binder::Status status =
        activatePackage(String8(args[1]).string());
    if (status.isOk()) {
      return OK;
    }
    std::string msg = StringLog()
            << "Failed to activate package: " << status.toString8().string()
            << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }
  if (args.size() == 2 && args[0] == String16("deactivatePackage")) {
    ::android::binder::Status status =
        deactivatePackage(String8(args[1]).string());
    if (status.isOk()) {
      return OK;
    }
    std::string msg = StringLog()
            << "Failed to deactivate package: " << status.toString8().string()
            << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }
  if (args.size() == 1 && args[0] == String16("help")) {
    print_help(out);
    return OK;
  }
  print_help(err);
  return BAD_VALUE;
}

}  // namespace

static constexpr const char* kApexServiceName = "apexservice";

using android::defaultServiceManager;
using android::IPCThreadState;
using android::ProcessState;
using android::sp;
using android::String16;

void CreateAndRegisterService() {
  sp<ProcessState> ps(ProcessState::self());

  // Create binder service and register with servicemanager
  sp<ApexService> apexService = new ApexService();
  defaultServiceManager()->addService(String16(kApexServiceName), apexService);
}

void JoinThreadPool() {
  sp<ProcessState> ps(ProcessState::self());

  // Start threadpool, wait for IPC
  ps->startThreadPool();
  IPCThreadState::self()->joinThreadPool();  // should not return
}

}  // namespace binder
}  // namespace apex
}  // namespace android
