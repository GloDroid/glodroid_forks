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
#include <android-base/file.h>
#include <android-base/logging.h>
#include <gtest/gtest.h>
#include <string>

#include "apex_file.h"

static std::string testDataDir =
    android::base::GetExecutableDirectory() + "/apexd_testdata";

namespace android {
namespace apex {

TEST(ApexFileTest, GetOffsetOfSimplePackage) {
  const std::string filePath = testDataDir + "/test.apex";
  StatusOr<std::unique_ptr<ApexFile>> apexFileRes = ApexFile::Open(filePath);
  ASSERT_TRUE(apexFileRes.Ok());
  ApexFile* apexFile = apexFileRes->get();
  EXPECT_EQ(8192, apexFile->GetImageOffset());
  EXPECT_EQ(589824u, apexFile->GetImageSize());
}

TEST(ApexFileTest, GetOffsetMissingFile) {
  const std::string filePath = testDataDir + "/missing.apex";
  StatusOr<std::unique_ptr<ApexFile>> apexFileRes = ApexFile::Open(filePath);
  ASSERT_FALSE(apexFileRes.Ok());
  EXPECT_NE(std::string::npos,
            apexFileRes.ErrorMessage().find("Failed to open package"))
      << apexFileRes.ErrorMessage();
}

TEST(ApexFileTest, GetApexManifest) {
  const std::string filePath = testDataDir + "/test.apex";
  StatusOr<std::unique_ptr<ApexFile>> apexFileRes = ApexFile::Open(filePath);
  ASSERT_TRUE(apexFileRes.Ok());
  ApexFile* apexFile = apexFileRes->get();
  EXPECT_EQ(
      "{\n  \"name\": \"com.android.example.apex\",\n  \"version\": 1\n}\n",
      std::string(apexFile->GetManifest()));
}
}  // namespace apex
}  // namespace android

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
