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
  StatusOr<ApexFile> apexFile = ApexFile::Open(filePath);
  ASSERT_TRUE(apexFile.Ok());
  EXPECT_EQ(8192, apexFile->GetImageOffset());
  EXPECT_EQ(589824u, apexFile->GetImageSize());
}

TEST(ApexFileTest, GetOffsetMissingFile) {
  const std::string filePath = testDataDir + "/missing.apex";
  StatusOr<ApexFile> apexFile = ApexFile::Open(filePath);
  ASSERT_FALSE(apexFile.Ok());
  EXPECT_NE(std::string::npos,
            apexFile.ErrorMessage().find("Failed to open package"))
      << apexFile.ErrorMessage();
}

TEST(ApexFileTest, GetApexManifest) {
  const std::string filePath = testDataDir + "/test.apex";
  StatusOr<ApexFile> apexFile = ApexFile::Open(filePath);
  ASSERT_TRUE(apexFile.Ok());
  EXPECT_EQ("com.android.example.apex", apexFile->GetManifest().GetName());
  EXPECT_EQ(1UL, apexFile->GetManifest().GetVersion());
}
}  // namespace apex
}  // namespace android

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
