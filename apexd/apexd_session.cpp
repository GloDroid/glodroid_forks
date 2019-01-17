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

#include "apexd_session.h"
#include "apexd.h"
#include "apexd_private.h"
#include "status_or.h"
#include "string_log.h"

#include "session_state.pb.h"

#include <android-base/logging.h>
#include <sys/stat.h>

#include <fstream>
using apex::proto::SessionState;

namespace android {
namespace apex {

namespace {

std::string getSessionDir(const int session_id) {
  return kApexSessionsDir + "/session_" + std::to_string(session_id);
}

std::string getSessionStateFile(const int session_id) {
  return getSessionDir(session_id) + "/state";
}

Status createDirIfNeeded(const std::string& path) {
  struct stat stat_data;

  if (stat(path.c_str(), &stat_data) != 0) {
    if (errno == ENOENT) {
      if (mkdir(path.c_str(), 0700) != 0) {
        return Status::Fail(PStringLog() << "Could not mkdir " << path);
      }

      return Status::Success();
    } else {
      return Status::Fail(PStringLog() << "Could not stat " << path);
    }
  }

  if (!S_ISDIR(stat_data.st_mode)) {
    return Status::Fail(path + " exists and is not a directory.");
  }

  return Status::Success();
}

StatusOr<std::string> createSessionDirIfNeeded(const int session_id) {
  // create /data/sessions
  auto res = createDirIfNeeded(kApexSessionsDir);
  if (!res.Ok()) {
    return StatusOr<std::string>(res.ErrorMessage());
  }
  // create /data/sessions/session_session_id
  std::string sessionDir = getSessionDir(session_id);
  res = createDirIfNeeded(sessionDir);
  if (!res.Ok()) {
    return StatusOr<std::string>(res.ErrorMessage());
  }

  return StatusOr<std::string>(sessionDir);
}

}  // namespace

StatusOr<SessionState> readSessionState(const int session_id) {
  SessionState sessionState;
  std::string stateFilePath = getSessionStateFile(session_id);

  std::fstream stateFile(stateFilePath, std::ios::in | std::ios::binary);
  if (!stateFile) {
    return StatusOr<SessionState>::MakeError("Could not open " + stateFilePath);
  }

  if (!sessionState.ParseFromIstream(&stateFile)) {
    return StatusOr<SessionState>::MakeError("Failed to parse " +
                                             stateFilePath);
  }

  return StatusOr<SessionState>(sessionState);
}

Status writeSessionState(const int session_id, SessionState session_state) {
  StatusOr<std::string> sessionDir = createSessionDirIfNeeded(session_id);
  if (!sessionDir.Ok()) {
    LOG(ERROR) << sessionDir.ErrorMessage();
    return sessionDir.ErrorStatus();
  }

  std::string stateFilePath = getSessionStateFile(session_id);
  std::fstream stateFile(stateFilePath,
                         std::ios::out | std::ios::trunc | std::ios::binary);
  if (!session_state.SerializeToOstream(&stateFile)) {
    return Status::Fail("Failed to write state file " + stateFilePath);
  }

  return Status::Success();
}

}  // namespace apex
}  // namespace android
