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

#include <android/apex/ApexSessionInfo.h>
#include <binder/IServiceManager.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "status.h"

using apex::proto::SessionState;

namespace android {
namespace apex {
namespace testing {

inline ::testing::AssertionResult IsOk(const Status& status) {
  if (status.Ok()) {
    return ::testing::AssertionSuccess() << " is Ok";
  } else {
    return ::testing::AssertionFailure()
           << " failed with " << status.ErrorMessage();
  }
}

template <typename T>
inline ::testing::AssertionResult IsOk(const StatusOr<T>& status_or) {
  if (status_or.Ok()) {
    return ::testing::AssertionSuccess() << " is Ok";
  } else {
    return ::testing::AssertionFailure()
           << " failed with " << status_or.ErrorMessage();
  }
}

inline ::testing::AssertionResult IsOk(const android::binder::Status& status) {
  if (status.isOk()) {
    return ::testing::AssertionSuccess() << " is Ok";
  } else {
    return ::testing::AssertionFailure()
           << " failed with " << status.toString8().c_str();
  }
}

MATCHER_P(SessionInfoEq, other, "") {
  using ::testing::AllOf;
  using ::testing::Eq;
  using ::testing::ExplainMatchResult;
  using ::testing::Field;

  return ExplainMatchResult(
      AllOf(
          Field("sessionId", &ApexSessionInfo::sessionId, Eq(other.sessionId)),
          Field("isUnknown", &ApexSessionInfo::isUnknown, Eq(other.isUnknown)),
          Field("isVerified", &ApexSessionInfo::isVerified,
                Eq(other.isVerified)),
          Field("isStaged", &ApexSessionInfo::isStaged, Eq(other.isStaged)),
          Field("isActivated", &ApexSessionInfo::isActivated,
                Eq(other.isActivated)),
          Field("isActivationFailed", &ApexSessionInfo::isActivationFailed,
                Eq(other.isActivationFailed)),
          Field("isSuccess", &ApexSessionInfo::isSuccess, Eq(other.isSuccess))),
      arg, result_listener);
}

inline ApexSessionInfo CreateSessionInfo(int session_id) {
  ApexSessionInfo info;
  info.sessionId = session_id;
  info.isUnknown = false;
  info.isVerified = false;
  info.isStaged = false;
  info.isActivated = false;
  info.isActivationFailed = false;
  info.isSuccess = false;
  return info;
}

}  // namespace testing
}  // namespace apex
}  // namespace android
