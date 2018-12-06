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

#include <algorithm>
#include <string>
#include <vector>

#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <binder/IServiceManager.h>
#include <gtest/gtest.h>
#include <selinux/selinux.h>

#include <android/apex/ApexInfo.h>
#include <android/apex/IApexService.h>

#include "status_or.h"

namespace android {
namespace apex {

using android::sp;
using android::String16;

class ApexServiceTest : public ::testing::Test {
 public:
  ApexServiceTest() {
    using android::IBinder;
    using android::IServiceManager;

    sp<IServiceManager> sm = android::defaultServiceManager();
    sp<IBinder> binder = sm->getService(String16("apexservice"));
    if (binder != nullptr) {
      service_ = android::interface_cast<IApexService>(binder);
    }
  }

  void SetUp() override { ASSERT_NE(nullptr, service_.get()); }

 protected:
  static std::string GetTestDataDir() {
    return android::base::GetExecutableDirectory() + "/apexd_testdata";
  }
  static std::string GetTestFile() { return GetTestDataDir() + "/test.apex"; }

  static bool HaveSelinux() { return 1 == is_selinux_enabled(); }

  static bool IsSelinuxEnforced() { return 0 != security_getenforce(); }

  StatusOr<bool> IsActive(const std::string& name, int64_t version) {
    std::vector<ApexInfo> list;
    android::binder::Status status = service_->getActivePackages(&list);
    if (status.isOk()) {
      for (const ApexInfo& p : list) {
        if (p.packageName == name && p.versionCode == version) {
          return StatusOr<bool>(true);
        }
      }
      return StatusOr<bool>(false);
    }
    return StatusOr<bool>::MakeError(status.toString8().c_str());
  }

  std::vector<std::string> GetActivePackagesStrings() {
    std::vector<ApexInfo> list;
    android::binder::Status status = service_->getActivePackages(&list);
    if (status.isOk()) {
      std::vector<std::string> ret;
      for (const ApexInfo& p : list) {
        ret.push_back(p.packageName + "@" + std::to_string(p.versionCode));
      }
      return ret;
    }

    std::vector<std::string> error;
    error.push_back("ERROR");
    return error;
  }

  struct PrepareTestApexForInstall {
    static constexpr const char* kTestDir = "/data/local/apexservice_tmp";
    static constexpr const char* kTestFile =
        "/data/local/apexservice_tmp/test.apex";
    static constexpr const char* kTestInstalled =
        "/data/apex/com.android.apex.test_package@1.apex";
    static constexpr const char* kTestName = "com.android.apex.test_package";

    bool Prepare() {
      auto prepare = []() {
        ASSERT_EQ(0, access(GetTestFile().c_str(), F_OK))
            << GetTestFile() << ": " << strerror(errno);
        ASSERT_EQ(0, mkdir(kTestDir, 0777)) << strerror(errno);

        auto mode_info = [](const std::string& f) {
          auto get_mode = [](const std::string& path) {
            struct stat buf;
            if (stat(path.c_str(), &buf) != 0) {
              return std::string(strerror(errno));
            }
            return android::base::StringPrintf("%x", buf.st_mode);
          };
          std::string file_part = f + "(" + get_mode(f) + ")";

          std::string dir = android::base::Dirname(f);
          std::string dir_part = dir + "(" + get_mode(dir) + ")";

          return file_part + " - " + dir_part;
        };
        int rc = link(GetTestFile().c_str(), kTestFile);
        if (rc != 0) {
          int saved_errno = errno;
          ASSERT_EQ(0, rc) << mode_info(GetTestFile()) << " to "
                           << mode_info(kTestFile) << " : "
                           << strerror(saved_errno);
        }

        ASSERT_EQ(0, chmod(kTestFile, 0777)) << strerror(errno);
        struct group* g = getgrnam("system");
        ASSERT_NE(nullptr, g);
        ASSERT_EQ(0, chown(kTestFile, /* root uid */ 0, g->gr_gid))
            << strerror(errno);

        ASSERT_TRUE(0 == setfilecon(kTestDir, "u:object_r:apex_data_file:s0") ||
                    !HaveSelinux())
            << strerror(errno);
        ASSERT_TRUE(0 ==
                        setfilecon(kTestFile, "u:object_r:apex_data_file:s0") ||
                    !HaveSelinux())
            << strerror(errno);
      };
      prepare();
      return !HasFatalFailure();
    }

    ~PrepareTestApexForInstall() {
      if (unlink(kTestFile) != 0) {
        PLOG(ERROR) << "Unable to unlink " << kTestFile;
      }
      if (rmdir(kTestDir) != 0) {
        PLOG(ERROR) << "Unable to rmdir " << kTestDir;
      }
    }
  };
  sp<IApexService> service_;
};

TEST_F(ApexServiceTest, HaveSelinux) {
  // We want to test under selinux.
  EXPECT_TRUE(HaveSelinux());
}

// Skip for b/119032200.
TEST_F(ApexServiceTest, DISABLED_EnforceSelinux) {
  // Crude cutout for virtual devices.
#if !defined(__i386__) && !defined(__x86_64__)
  constexpr bool kIsX86 = false;
#else
  constexpr bool kIsX86 = true;
#endif
  EXPECT_TRUE(IsSelinuxEnforced() || kIsX86);
}

TEST_F(ApexServiceTest, StageFailAccess) {
  if (!IsSelinuxEnforced()) {
    LOG(WARNING) << "Skipping InstallFailAccess because of selinux";
    return;
  }

  // Use an extra copy, so that even if this test fails (incorrectly installs),
  // we have the testdata file still around.
  std::string orig_test_file = GetTestFile();
  std::string test_file = orig_test_file + ".2";
  ASSERT_EQ(0, link(orig_test_file.c_str(), test_file.c_str()))
      << strerror(errno);
  struct Deleter {
    std::string to_delete;
    Deleter(const std::string& t) : to_delete(t) {}
    ~Deleter() {
      if (unlink(to_delete.c_str()) != 0) {
        PLOG(ERROR) << "Could not unlink " << to_delete;
      }
    }
  };
  Deleter del(test_file);

  bool success;
  android::binder::Status st = service_->stagePackage(test_file, &success);
  ASSERT_FALSE(st.isOk());
  std::string error = st.toString8().c_str();
  EXPECT_NE(std::string::npos, error.find("Failed to open package")) << error;
  EXPECT_NE(std::string::npos, error.find("I/O error")) << error;
}

TEST_F(ApexServiceTest, StageSuccess) {
  PrepareTestApexForInstall installer;
  if (!installer.Prepare()) {
    return;
  }

  bool success;
  android::binder::Status st =
      service_->stagePackage(PrepareTestApexForInstall::kTestFile, &success);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
  ASSERT_TRUE(success);

  // TODO: Uninstall.
}

TEST_F(ApexServiceTest, Activate) {
  PrepareTestApexForInstall installer;
  if (!installer.Prepare()) {
    return;
  }

  {
    // Check package is not active.
    StatusOr<bool> active = IsActive(PrepareTestApexForInstall::kTestName, 1);
    ASSERT_TRUE(active.Ok());
    ASSERT_FALSE(*active);
  }

  {
    bool success;
    android::binder::Status st =
        service_->stagePackage(PrepareTestApexForInstall::kTestFile, &success);
    ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
    ASSERT_TRUE(success);
  }

  android::binder::Status st =
      service_->activatePackage(PrepareTestApexForInstall::kTestInstalled);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();

  {
    // Check package is active.
    StatusOr<bool> active = IsActive(PrepareTestApexForInstall::kTestName, 1);
    ASSERT_TRUE(active.Ok());
    ASSERT_TRUE(*active) << android::base::Join(GetActivePackagesStrings(),
                                                ',');
  }

  {
    // Check that the "latest" view exists.
    std::string latest_path =
        std::string("/apex/") + PrepareTestApexForInstall::kTestName;
    struct stat buf;
    ASSERT_EQ(0, stat(latest_path.c_str(), &buf)) << strerror(errno);
    // Check that it is a folder.
    EXPECT_TRUE(S_ISDIR(buf.st_mode));

    // Collect direct entries of a folder.
    auto collect_entries_fn = [](const std::string& path) {
      std::vector<std::string> ret;
      // Check that there is something in there.
      auto d =
          std::unique_ptr<DIR, int (*)(DIR*)>(opendir(path.c_str()), closedir);
      if (d == nullptr) {
        return ret;
      }

      struct dirent* dp;
      while ((dp = readdir(d.get())) != nullptr) {
        if (dp->d_type != DT_DIR || (strcmp(dp->d_name, ".") == 0) ||
            (strcmp(dp->d_name, "..") == 0)) {
          continue;
        }
        ret.emplace_back(dp->d_name);
      }
      std::sort(ret.begin(), ret.end());
      return ret;
    };

    std::vector<std::string> versioned_folder_entries = collect_entries_fn(
        std::string("/apex/") + PrepareTestApexForInstall::kTestName + "@1");
    std::vector<std::string> latest_folder_entries =
        collect_entries_fn(latest_path);

    EXPECT_TRUE(versioned_folder_entries == latest_folder_entries)
        << "Versioned: " << android::base::Join(versioned_folder_entries, ',')
        << " Latest:" << android::base::Join(latest_folder_entries, ',');
  }

  // Cleanup.
  st = service_->deactivatePackage(PrepareTestApexForInstall::kTestInstalled);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
  {
    // Check package is not active.
    StatusOr<bool> active = IsActive(PrepareTestApexForInstall::kTestName, 1);
    ASSERT_TRUE(active.Ok());
    ASSERT_FALSE(*active) << android::base::Join(GetActivePackagesStrings(),
                                                 ',');
  }

  // TODO: Uninstall.
}

class LogTestToLogcat : public testing::EmptyTestEventListener {
  void OnTestStart(const testing::TestInfo& test_info) override {
#ifdef __ANDROID__
    using base::LogId;
    using base::LogSeverity;
    using base::StringPrintf;
    base::LogdLogger l;
    std::string msg =
        StringPrintf("=== %s::%s (%s:%d)", test_info.test_case_name(),
                     test_info.name(), test_info.file(), test_info.line());
    l(LogId::MAIN, LogSeverity::INFO, "apexservice_test", __FILE__, __LINE__,
      msg.c_str());
#else
    UNUSED(test_info);
#endif
  }
};

}  // namespace apex
}  // namespace android

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  ::testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(
      new android::apex::LogTestToLogcat());
  return RUN_ALL_TESTS();
}
