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
#include <binder/Status.h>
#include <utils/String16.h>

#include "apexd.h"
#include "apexd_session.h"
#include "status.h"
#include "string_log.h"

#include <android/apex/BnApexService.h>

namespace android {
namespace apex {
namespace binder {
namespace {

using BinderStatus = ::android::binder::Status;

class ApexService : public BnApexService {
 public:
  using BinderStatus = ::android::binder::Status;
  using SessionState = ::apex::proto::SessionState;

  ApexService(){};

  BinderStatus stagePackage(const std::string& packageTmpPath,
                            bool* aidl_return) override;
  BinderStatus stagePackages(const std::vector<std::string>& paths,
                             bool* aidl_return) override;
  BinderStatus submitStagedSession(int session_id,
                                   const std::vector<int>& child_session_ids,
                                   ApexInfoList* apex_info_list,
                                   bool* aidl_return) override;
  BinderStatus getStagedSessionInfo(
      int session_id, ApexSessionInfo* apex_session_info) override;
  BinderStatus activatePackage(const std::string& packagePath) override;
  BinderStatus deactivatePackage(const std::string& packagePath) override;
  BinderStatus getActivePackages(std::vector<ApexInfo>* aidl_return) override;
  BinderStatus getActivePackage(const std::string& packageName,
                                ApexInfo* aidl_return) override;
  BinderStatus preinstallPackages(
      const std::vector<std::string>& paths) override;
  BinderStatus postinstallPackages(
      const std::vector<std::string>& paths) override;

  status_t dump(int fd, const Vector<String16>& args) override;

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
  std::vector<std::string> tmp;
  tmp.push_back(packageTmpPath);
  return stagePackages(tmp, aidl_return);
}

BinderStatus ApexService::stagePackages(const std::vector<std::string>& paths,
                                        bool* aidl_return) {
  LOG(DEBUG) << "stagePackages() received by ApexService, paths "
             << android::base::Join(paths, ',');

  *aidl_return = false;
  Status res = ::android::apex::stagePackages(paths);

  if (res.Ok()) {
    *aidl_return = true;
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to stage " << android::base::Join(paths, ',') << ": "
             << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::submitStagedSession(
    int session_id, const std::vector<int>& child_session_ids,
    ApexInfoList* apex_info_list, bool* aidl_return) {
  LOG(DEBUG) << "submitStagedSession() received by ApexService, session id "
             << session_id;

  StatusOr<std::vector<ApexFile>> packages =
      ::android::apex::submitStagedSession(session_id, child_session_ids);
  if (!packages.Ok()) {
    *aidl_return = false;
    LOG(ERROR) << "Failed to submit session id " << session_id << ": "
               << packages.ErrorMessage();
    return BinderStatus::ok();
  }

  for (const auto& package : *packages) {
    ApexInfo out;
    out.packageName = package.GetManifest().name();
    out.packagePath = package.GetPath();
    out.versionCode = package.GetManifest().version();
    apex_info_list->apexInfos.push_back(out);
  }
  *aidl_return = true;
  return BinderStatus::ok();
}

BinderStatus ApexService::getStagedSessionInfo(
    int session_id, ApexSessionInfo* apex_session_info) {
  LOG(DEBUG) << "getStagedSessionInfo() received by ApexService, session id "
             << session_id;
  apex_session_info->isUnknown = true;
  apex_session_info->isVerified = false;
  apex_session_info->isStaged = false;
  apex_session_info->isActivated = false;
  apex_session_info->isActivationPendingRetry = false;
  apex_session_info->isActivationFailed = false;
  auto session = ApexSession::GetSession(session_id);
  if (!session.Ok()) {
    // Unknown session.
    return BinderStatus::ok();
  }
  apex_session_info->isUnknown = false;
  switch ((*session).GetState()) {
    case SessionState::VERIFIED:
      apex_session_info->isVerified = true;
      break;
    case SessionState::STAGED:
      apex_session_info->isStaged = true;
      break;
    case SessionState::ACTIVATED:
      apex_session_info->isActivated = true;
      break;
    case SessionState::ACTIVATION_PENDING_RETRY:
      apex_session_info->isActivationPendingRetry = true;
      break;
    case SessionState::ACTIVATION_FAILED:
      apex_session_info->isActivationFailed = true;
      break;
    case SessionState::UNKNOWN:
    default:
      apex_session_info->isUnknown = true;
      break;
  }

  return BinderStatus::ok();
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
  auto packages = ::android::apex::getActivePackages();
  for (const auto& package : packages) {
    ApexInfo out;
    out.packageName = package.GetManifest().name();
    out.packagePath = package.GetPath();
    out.versionCode = package.GetManifest().version();
    aidl_return->push_back(out);
  }

  return BinderStatus::ok();
}

BinderStatus ApexService::getActivePackage(const std::string& packageName,
                                           ApexInfo* aidl_return) {
  StatusOr<ApexFile> apex = ::android::apex::getActivePackage(packageName);
  if (apex.Ok()) {
    aidl_return->packageName = apex->GetManifest().name();
    aidl_return->packagePath = apex->GetPath();
    aidl_return->versionCode = apex->GetManifest().version();
  }

  return BinderStatus::ok();
}

BinderStatus ApexService::preinstallPackages(
    const std::vector<std::string>& paths) {
  BinderStatus debugCheck = CheckDebuggable("preinstallPackages");
  if (!debugCheck.isOk()) {
    return debugCheck;
  }

  Status res = ::android::apex::preinstallPackages(paths);
  if (res.Ok()) {
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to preinstall packages "
             << android::base::Join(paths, ',') << ": " << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
}

BinderStatus ApexService::postinstallPackages(
    const std::vector<std::string>& paths) {
  BinderStatus debugCheck = CheckDebuggable("postinstallPackages");
  if (!debugCheck.isOk()) {
    return debugCheck;
  }

  Status res = ::android::apex::postinstallPackages(paths);
  if (res.Ok()) {
    return BinderStatus::ok();
  }

  // TODO: Get correct binder error status.
  LOG(ERROR) << "Failed to postinstall packages "
             << android::base::Join(paths, ',') << ": " << res.ErrorMessage();
  return BinderStatus::fromExceptionCode(BinderStatus::EX_ILLEGAL_ARGUMENT,
                                         String8(res.ErrorMessage().c_str()));
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
status_t ApexService::dump(int fd, const Vector<String16>& args) {
  // TODO: Extend to add session info
  std::vector<ApexInfo> list;
  BinderStatus status = getActivePackages(&list);
  if (status.isOk()) {
    for (const auto& item : list) {
      std::string msg = StringLog()
                        << "Package: " << item.packageName
                        << " Version: " << item.versionCode
                        << " Path: " << item.packagePath << std::endl;
      dprintf(fd, "%s", msg.c_str());
    }
    return OK;
  }
  std::string msg = StringLog() << "Failed to retrieve packages: "
                                << status.toString8().string() << std::endl;
  dprintf(fd, "%s", msg.c_str());
  return BAD_VALUE;
}

status_t ApexService::shellCommand(int in, int out, int err,
                                   const Vector<String16>& args) {
  if (in == BAD_TYPE || out == BAD_TYPE || err == BAD_TYPE) {
    return BAD_VALUE;
  }
  auto print_help = [](int fd, const char* prefix = nullptr) {
    StringLog log;
    if (prefix != nullptr) {
      log << prefix << std::endl;
    }
    log << "ApexService:" << std::endl
        << "  help - display this help" << std::endl
        << "  stagePackage [packagePath] - stage package from the given path"
        << std::endl
        << "  stagePackages [packagePath1] ([packagePath2]...) - stage "
           "multiple packages from the given path"
        << std::endl
        << "  getActivePackage [packageName] - return info for active package "
           "with given name, if present"
        << std::endl
        << "  getActivePackages - return the list of active packages"
        << std::endl
        << "  activatePackage [packagePath] - activate package from the "
           "given path"
        << std::endl
        << "  deactivatePackage [packagePath] - deactivate package from the "
           "given path"
        << std::endl
        << "  getStagedSessionInfo [sessionId] - displays information about a "
           "given session previously submitted"
        << "  submitStagedSession [sessionId] - attempts to submit the "
           "installer session with given id"
        << std::endl;
    dprintf(fd, "%s", log.operator std::string().c_str());
  };

  if (args.size() == 0) {
    print_help(err, "No command given");
    return BAD_VALUE;
  }

  const String16& cmd = args[0];

  if (cmd == String16("stagePackage") || cmd == String16("stagePackages")) {
    if (args.size() < 2) {
      print_help(err, "stagePackage(s) requires at least one packagePath");
      return BAD_VALUE;
    }
    if (args.size() != 2 && cmd == String16("stagePackage")) {
      print_help(err, "stagePackage requires one packagePath");
      return BAD_VALUE;
    }
    std::vector<std::string> pkgs;
    pkgs.reserve(args.size() - 1);
    for (size_t i = 1; i != args.size(); ++i) {
      pkgs.emplace_back(String8(args[i]).string());
    }
    bool ret_value;
    BinderStatus status = stagePackages(pkgs, &ret_value);
    if (status.isOk()) {
      return OK;
    }
    std::string msg = StringLog() << "Failed to stage package(s): "
                                  << status.toString8().string() << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("getActivePackages")) {
    if (args.size() != 1) {
      print_help(err, "Unrecognized options");
      return BAD_VALUE;
    }
    std::vector<ApexInfo> list;
    BinderStatus status = getActivePackages(&list);
    if (status.isOk()) {
      for (const auto& item : list) {
        std::string msg = StringLog()
                          << "Package: " << item.packageName
                          << " Version: " << item.versionCode
                          << " Path: " << item.packagePath << std::endl;
        dprintf(out, "%s", msg.c_str());
      }
      return OK;
    }
    std::string msg = StringLog() << "Failed to retrieve packages: "
                                  << status.toString8().string() << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("getActivePackage")) {
    if (args.size() != 2) {
      print_help(err, "Unrecognized options");
      return BAD_VALUE;
    }

    ApexInfo package;
    BinderStatus status = getActivePackage(String8(args[1]).string(), &package);
    if (status.isOk()) {
      std::string msg = StringLog()
                        << "Package: " << package.packageName
                        << " Version: " << package.versionCode
                        << " Path: " << package.packagePath << std::endl;
      return OK;
    }

    std::string msg = StringLog() << "Failed to fetch active package: "
                                  << String8(args[1]).string()
                                  << ", error: " << status.toString8().string()
                                  << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("activatePackage")) {
    if (args.size() != 2) {
      print_help(err, "activatePackage requires one packagePath");
      return BAD_VALUE;
    }
    BinderStatus status = activatePackage(String8(args[1]).string());
    if (status.isOk()) {
      return OK;
    }
    std::string msg = StringLog() << "Failed to activate package: "
                                  << status.toString8().string() << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("deactivatePackage")) {
    if (args.size() != 2) {
      print_help(err, "deactivatePackage requires one packagePath");
      return BAD_VALUE;
    }
    BinderStatus status = deactivatePackage(String8(args[1]).string());
    if (status.isOk()) {
      return OK;
    }
    std::string msg = StringLog() << "Failed to deactivate package: "
                                  << status.toString8().string() << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("getStagedSessionInfo")) {
    if (args.size() != 2) {
      print_help(err, "getStagedSessionInfo requires one session id");
      return BAD_VALUE;
    }
    int session_id = strtol(String8(args[1]).c_str(), nullptr, 10);
    if (session_id < 0) {
      std::string msg = StringLog()
                        << "Failed to parse session id. Must be an integer.";
      dprintf(err, "%s", msg.c_str());
      return BAD_VALUE;
    }

    ApexSessionInfo session_info;
    BinderStatus status = getStagedSessionInfo(session_id, &session_info);
    if (status.isOk()) {
      std::string msg = StringLog()
                        << "session_info: "
                        << " isUnknown: " << session_info.isUnknown
                        << " isVerified: " << session_info.isVerified
                        << " isStaged: " << session_info.isStaged
                        << " isActivated: " << session_info.isActivated
                        << " isActivationPendingRetry: "
                        << session_info.isActivationPendingRetry
                        << " isActivationFailed: "
                        << session_info.isActivationFailed << std::endl;
      dprintf(out, "%s", msg.c_str());
      return OK;
    }
    std::string msg = StringLog() << "Failed to query session: "
                                  << status.toString8().string() << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("submitStagedSession")) {
    if (args.size() != 2) {
      print_help(err, "submitStagedSession requires one session id");
      return BAD_VALUE;
    }
    int session_id = strtol(String8(args[1]).c_str(), nullptr, 10);
    if (session_id < 0) {
      std::string msg = StringLog()
                        << "Failed to parse session id. Must be an integer.";
      dprintf(err, "%s", msg.c_str());
      return BAD_VALUE;
    }

    ApexInfoList list;
    std::vector<int> empty_child_session_ids;
    bool ret_value;

    BinderStatus status = submitStagedSession(
        session_id, empty_child_session_ids, &list, &ret_value);
    if (status.isOk()) {
      if (ret_value) {
        for (const auto& item : list.apexInfos) {
          std::string msg = StringLog()
                            << "Package: " << item.packageName
                            << " Version: " << item.versionCode
                            << " Path: " << item.packagePath << std::endl;
          dprintf(out, "%s", msg.c_str());
        }
      } else {
        std::string msg = StringLog() << "Verification failed." << std::endl;
        dprintf(out, "%s", msg.c_str());
      }
      return OK;
    }
    std::string msg = StringLog() << "Failed to submit session: "
                                  << status.toString8().string() << std::endl;
    dprintf(err, "%s", msg.c_str());
    return BAD_VALUE;
  }

  if (cmd == String16("help")) {
    if (args.size() != 1) {
      print_help(err, "Help has no options");
      return BAD_VALUE;
    }
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
