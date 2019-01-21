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
#include "apexd_utils.h"
#include "status_or.h"
#include "string_log.h"

#include "session_state.pb.h"

#include <android-base/logging.h>
#include <dirent.h>
#include <sys/stat.h>

#include <fstream>
using apex::proto::SessionState;

namespace android {
namespace apex {

namespace {

std::string getSessionDir(int session_id) {
  return kApexSessionsDir + "/" + std::to_string(session_id);
}

std::string getSessionStateFilePath(int session_id) {
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

StatusOr<std::string> createSessionDirIfNeeded(int session_id) {
  // create /data/sessions
  auto res = createDirIfNeeded(kApexSessionsDir);
  if (!res.Ok()) {
    return StatusOr<std::string>(res.ErrorMessage());
  }
  // create /data/sessions/session_id
  std::string sessionDir = getSessionDir(session_id);
  res = createDirIfNeeded(sessionDir);
  if (!res.Ok()) {
    return StatusOr<std::string>(res.ErrorMessage());
  }

  return StatusOr<std::string>(sessionDir);
}

int getSessionIdFromSessionDir(const std::string& session_dir) {
  int sessionId;
  std::string sessionDirFormat = std::string(kApexSessionsDir) + "/%d";

  // Not using std::stoi because it throws exceptions when it can't match
  int numFound =
      sscanf(session_dir.c_str(), sessionDirFormat.c_str(), &sessionId);
  if (numFound == 1) {
    return sessionId;
  } else {
    return -1;
  }
}

}  // namespace

ApexSession::ApexSession(int id, SessionState state) : id_(id), state_(state) {}

StatusOr<ApexSession> ApexSession::CreateSession(int session_id) {
  // Create session directory
  auto sessionPath = createSessionDirIfNeeded(session_id);
  if (!sessionPath.Ok()) {
    return StatusOr<ApexSession>::MakeError(sessionPath.ErrorMessage());
  }
  ApexSession session(session_id, SessionState());

  return StatusOr<ApexSession>(std::move(session));
}

StatusOr<ApexSession> ApexSession::GetSession(int session_id) {
  SessionState state;
  auto path = getSessionStateFilePath(session_id);
  std::fstream stateFile(path, std::ios::in | std::ios::binary);
  if (!stateFile) {
    return StatusOr<ApexSession>::MakeError("Failed to open " + path);
  }

  if (!state.ParseFromIstream(&stateFile)) {
    return StatusOr<ApexSession>::MakeError("Failed to parse " + path);
  }

  return StatusOr<ApexSession>(ApexSession(session_id, state));
}

std::vector<ApexSession> ApexSession::GetSessions() {
  std::vector<ApexSession> sessions;

  StatusOr<std::vector<std::string>> sessionPaths =
      ReadDir(kApexSessionsDir, [](unsigned char d_type, const char* d_name) {
        return (d_type == DT_DIR);
      });

  if (!sessionPaths.Ok()) {
    return sessions;
  }

  for (const std::string sessionDirPath : *sessionPaths) {
    // Try to read session state
    int sessionId = getSessionIdFromSessionDir(sessionDirPath);
    if (sessionId == -1) {
      LOG(WARNING) << "Could not parse session ID from " << sessionDirPath;
      continue;
    }
    auto session = GetSession(sessionId);
    if (!session.Ok()) {
      LOG(WARNING) << session.ErrorMessage();
      continue;
    }
    sessions.push_back(std::move(*session));
  }

  return sessions;
}

std::vector<ApexSession> ApexSession::GetSessionsInState(
    SessionState::State state) {
  auto sessions = GetSessions();
  sessions.erase(
      std::remove_if(sessions.begin(), sessions.end(),
                     [&](ApexSession s) { return s.GetState() != state; }),
      sessions.end());

  return sessions;
}

SessionState::State ApexSession::GetState() const { return state_.state(); }

int ApexSession::GetId() const { return id_; }

Status ApexSession::UpdateStateAndCommit(SessionState::State session_state) {
  state_.set_state(session_state);

  auto stateFilePath = getSessionStateFilePath(id_);

  std::fstream stateFile(stateFilePath,
                         std::ios::out | std::ios::trunc | std::ios::binary);
  if (!state_.SerializeToOstream(&stateFile)) {
    return Status::Fail("Failed to write state file " + stateFilePath);
  }

  return Status::Success();
}

}  // namespace apex
}  // namespace android
