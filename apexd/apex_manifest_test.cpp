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
#include <android-base/logging.h>
#include <gtest/gtest.h>

#include "apex_manifest.h"

namespace android {
namespace apex {

TEST(ApexManifestTest, SimpleTest) {
  auto apexManifest = ApexManifest::Parse(
      "{\"name\": \"com.android.example.apex\", \"version\": 1}\n");
  ASSERT_TRUE(apexManifest.Ok());
  EXPECT_EQ("com.android.example.apex", std::string(apexManifest->GetName()));
  EXPECT_EQ(1u, apexManifest->GetVersion());
}

TEST(ApexManifestTest, NameMissing) {
  auto apexManifest = ApexManifest::Parse("{\"version\": 1}\n");
  ASSERT_FALSE(apexManifest.Ok());
  EXPECT_EQ(apexManifest.ErrorMessage(),
            std::string("Missing required field \"name\" from APEX manifest."))
      << apexManifest.ErrorMessage();
}

TEST(ApexManifestTest, VersionMissing) {
  auto apexManifest =
      ApexManifest::Parse("{\"name\": \"com.android.example.apex\"}\n");
  ASSERT_FALSE(apexManifest.Ok());
  EXPECT_EQ(
      apexManifest.ErrorMessage(),
      std::string("Missing required field \"version\" from APEX manifest."))
      << apexManifest.ErrorMessage();
}

TEST(ApexManifestTest, VersionNotNumber) {
  auto apexManifest = ApexManifest::Parse(
      "{\"name\": \"com.android.example.apex\", \"version\": \"1\"}\n");
  ASSERT_FALSE(apexManifest.Ok());
  EXPECT_EQ(apexManifest.ErrorMessage(),
            std::string("Invalid type for field \"version\" from APEX "
                        "manifest, expecting integer."))
      << apexManifest.ErrorMessage();
}

TEST(ApexManifestTest, NoPreInstallHook) {
  auto apexManifest = ApexManifest::Parse(
      "{\"name\": \"com.android.example.apex\", \"version\": 1}\n");
  ASSERT_TRUE(apexManifest.Ok());
  EXPECT_EQ("", std::string(apexManifest->GetPreInstallHook()));
}

TEST(ApexManifestTest, PreInstallHook) {
  auto apexManifest = ApexManifest::Parse(
      "{\"name\": \"com.android.example.apex\", \"version\": 1, "
      "\"pre_install_hook\": \"bin/pre_install_hook\"}\n");
  ASSERT_TRUE(apexManifest.Ok());
  EXPECT_EQ("bin/pre_install_hook",
            std::string(apexManifest->GetPreInstallHook()));
}

TEST(ApexManifestTest, NoPostInstallHook) {
  auto apexManifest = ApexManifest::Parse(
      "{\"name\": \"com.android.example.apex\", \"version\": 1}\n");
  ASSERT_TRUE(apexManifest.Ok());
  EXPECT_EQ("", std::string(apexManifest->GetPostInstallHook()));
}

TEST(ApexManifestTest, PostInstallHook) {
  auto apexManifest = ApexManifest::Parse(
      "{\"name\": \"com.android.example.apex\", \"version\": 1, "
      "\"post_install_hook\": \"bin/post_install_hook\"}\n");
  ASSERT_TRUE(apexManifest.Ok());
  EXPECT_EQ("bin/post_install_hook",
            std::string(apexManifest->GetPostInstallHook()));
}

TEST(ApexManifestTest, UnparsableManifest) {
  auto apexManifest = ApexManifest::Parse("This is an invalid pony");
  ASSERT_FALSE(apexManifest.Ok());
  EXPECT_EQ(
      apexManifest.ErrorMessage(),
      std::string(
          "Failed to parse APEX Manifest JSON config: * Line 1, Column 1\n"
          "  Syntax error: value, object or array expected.\n"))
      << apexManifest.ErrorMessage();
}

}  // namespace apex
}  // namespace android

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
