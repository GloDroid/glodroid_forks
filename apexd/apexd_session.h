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

#ifndef ANDROID_APEXD_APEXD_SESSION_H_
#define ANDROID_APEXD_APEXD_SESSION_H_

#include "apexd.h"
#include "status_or.h"

#include "session_state.pb.h"

namespace android {
namespace apex {

static const std::string kApexSessionsDir =
    std::string(kApexPackageDataDir) + "/sessions";

class ApexSession {
 public:
  static StatusOr<ApexSession> CreateSession(int session_id);
  static StatusOr<ApexSession> GetSession(int session_id);
  static std::vector<ApexSession> GetSessions();
  static std::vector<ApexSession> GetSessionsInState(
      ::apex::proto::SessionState::State state);
  ApexSession() = delete;

  const google::protobuf::RepeatedField<int> GetChildSessionIds() const;
  ::apex::proto::SessionState::State GetState() const;
  int GetId() const;

  void SetChildSessionIds(const std::vector<int>& child_session_ids);
  Status UpdateStateAndCommit(::apex::proto::SessionState::State state);

 private:
  ApexSession(int id, ::apex::proto::SessionState state);
  int id_;
  ::apex::proto::SessionState state_;
};

}  // namespace apex
}  // namespace android

#endif  // ANDROID_APEXD_APEXD_SESSION_H
