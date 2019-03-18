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

#include <filesystem>
#include <string>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <gtest/gtest.h>

#include "apex_shim.h"

namespace android {
namespace apex {
namespace shim {

namespace {

using android::base::StringPrintf;

namespace fs = std::filesystem;

class ApexShimTest : public ::testing::Test {
 protected:
  static std::string GetTestDataDir() {
    return android::base::GetExecutableDirectory();
  }
  static std::string GetTestFile(const std::string& name) {
    return GetTestDataDir() + "/" + name;
  }
  static std::string GetTestTempDir() {
    using ::testing::UnitTest;
    return StringPrintf("%s/%s", GetTestDataDir().c_str(),
                        UnitTest::GetInstance()->current_test_info()->name());
  }

  void SetUp() override {
    std::error_code ec;
    fs::create_directory(GetTestTempDir(), ec);
    ASSERT_FALSE(ec) << "Failed to create " << GetTestTempDir() << " : " << ec;
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(GetTestTempDir(), ec);
    ASSERT_FALSE(ec) << "Failed to delete " << GetTestTempDir() << " : " << ec;
  }
};

// TODO(ioffe): use apexd_test_utils.h after creating libapextestutils
TEST_F(ApexShimTest, ValidateUpdateSuccess) {
  std::string fake_mount_point =
      StringPrintf(GetTestTempDir().c_str(), "fake_mount_point");
  std::error_code ec;
  fs::create_directory(fake_mount_point, ec);
  ASSERT_FALSE(ec) << "Failed to create " << fake_mount_point << " : " << ec;

  std::string test_apex = GetTestFile("apex.apexd_test.apex");
  std::string calc_sha512_cmd =
      StringPrintf("sha512sum -b %s | awk '{print $1}' > %s/hash.txt",
                   test_apex.c_str(), fake_mount_point.c_str());
  system(calc_sha512_cmd.c_str());

  auto ret = ValidateUpdate(fake_mount_point, test_apex);
  ASSERT_TRUE(ret.Ok()) << ret.ErrorMessage();
}

TEST_F(ApexShimTest, ValidateUpdateFaiureWrongHash) {
  std::string fake_mount_point =
      StringPrintf(GetTestTempDir().c_str(), "fake_mount_point");
  std::error_code ec;
  fs::create_directory(fake_mount_point, ec);
  ASSERT_FALSE(ec) << "Failed to create " << fake_mount_point << " : " << ec;

  std::string test_apex = GetTestFile("apex.apexd_test.apex");
  std::string calc_sha512_cmd =
      StringPrintf("sha512sum -b /dev/null | awk '{print $1}' > %s/hash.txt",
                   fake_mount_point.c_str());
  system(calc_sha512_cmd.c_str());

  auto ret = ValidateUpdate(fake_mount_point, test_apex);
  ASSERT_FALSE(ret.Ok());
}

TEST_F(ApexShimTest, ValidateUpdateFaiureCanNotReadHash) {
  std::string fake_mount_point =
      StringPrintf(GetTestTempDir().c_str(), "fake_mount_point");
  std::error_code ec;
  fs::create_directory(fake_mount_point, ec);
  ASSERT_FALSE(ec) << "Failed to create " << fake_mount_point << " : " << ec;

  std::string test_apex = GetTestFile("apex.apexd_test.apex");

  auto ret = ValidateUpdate(fake_mount_point, test_apex);
  ASSERT_FALSE(ret.Ok());
}

}  // namespace
}  // namespace shim
}  // namespace apex
}  // namespace android

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
