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

#ifndef ANDROID_APEX_APEX_SERVICE_H
#define ANDROID_APEX_APEX_SERVICE_H

#include <android/apex/BnApexService.h>

namespace android {

class String16;
template <typename T>
class Vector;

namespace apex {

class ApexService : public BnApexService {
 public:
  using BinderStatus = ::android::binder::Status;

  ApexService(){};

  BinderStatus stagePackage(const std::string& packageTmpPath,
                            bool* aidl_return) override;
  BinderStatus activatePackage(const std::string& packagePath) override;
  BinderStatus deactivatePackage(const std::string& packagePath) override;
  BinderStatus getActivePackages(
      std::vector<PackageInfo>* aidl_return) override;

  // Override onTransact so we can handle shellCommand.
  status_t onTransact(uint32_t _aidl_code, const Parcel& _aidl_data,
                      Parcel* _aidl_reply, uint32_t _aidl_flags = 0) override;

  status_t shellCommand(int in, int out, int err, const Vector<String16>& args);
};

}  // namespace apex
}  // namespace android

#endif  // ANDROID_APEX_APEX_SERVICE_H
