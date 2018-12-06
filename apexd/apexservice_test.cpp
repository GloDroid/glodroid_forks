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
#include <android-base/scopeguard.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <binder/IServiceManager.h>
#include <gtest/gtest.h>
#include <selinux/selinux.h>

#include <android/apex/ApexInfo.h>
#include <android/apex/IApexService.h>

#include "apex_file.h"
#include "apex_manifest.h"
#include "apexd.h"
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
  static std::string GetTestFile(const std::string& name) {
    return GetTestDataDir() + "/" + name;
  }

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

    // This is given to the constructor.
    std::string test_input;  // Original test file.

    // This is derived from the input.
    std::string test_file;            // Prepared path. Under kTestDir.
    std::string test_installed_file;  // Where apexd will store it.

    std::string package;  // APEX package name.
    uint64_t version;     // APEX version.

    PrepareTestApexForInstall(const std::string& test) {
      test_input = test;

      test_file = std::string(kTestDir) + "/" + android::base::Basename(test);

      StatusOr<ApexFile> apex_file = ApexFile::Open(test);
      CHECK(apex_file.Ok());

      const ApexManifest& manifest = apex_file->GetManifest();
      package = manifest.GetName();
      version = manifest.GetVersion();

      test_installed_file = std::string(kApexPackageDataDir) + "/" + package +
                            "@" + std::to_string(version) + ".apex";
    }

    bool Prepare() {
      auto prepare = [](const std::string& src, const std::string& trg) {
        ASSERT_EQ(0, access(src.c_str(), F_OK))
            << src << ": " << strerror(errno);
        const std::string trg_dir = android::base::Dirname(trg);
        ASSERT_EQ(0, mkdir(trg_dir.c_str(), 0777)) << trg << strerror(errno);

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
        int rc = link(src.c_str(), trg.c_str());
        if (rc != 0) {
          int saved_errno = errno;
          ASSERT_EQ(0, rc) << mode_info(src) << " to " << mode_info(trg)
                           << " : " << strerror(saved_errno);
        }

        ASSERT_EQ(0, chmod(trg.c_str(), 0666)) << strerror(errno);
        struct group* g = getgrnam("system");
        ASSERT_NE(nullptr, g);
        ASSERT_EQ(0, chown(trg.c_str(), /* root uid */ 0, g->gr_gid))
            << strerror(errno);

        rc = setfilecon(trg_dir.c_str(), "u:object_r:apex_data_file:s0");
        ASSERT_TRUE(0 == rc || !HaveSelinux()) << strerror(errno);
        rc = setfilecon(trg.c_str(), "u:object_r:apex_data_file:s0");
        ASSERT_TRUE(0 == rc || !HaveSelinux()) << strerror(errno);
      };
      prepare(test_input, test_file);
      return !HasFatalFailure();
    }

    ~PrepareTestApexForInstall() {
      if (unlink(test_file.c_str()) != 0) {
        PLOG(ERROR) << "Unable to unlink " << test_file;
      }
      if (rmdir(kTestDir) != 0) {
        PLOG(ERROR) << "Unable to rmdir " << kTestDir;
      }

      // For cleanliness, also attempt to delete apexd's file.
      // TODO: to the unstaging using APIs
      if (unlink(test_installed_file.c_str()) != 0) {
        PLOG(ERROR) << "Unable to unlink " << test_installed_file;
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
  std::string orig_test_file = GetTestFile("test.apex");
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
  PrepareTestApexForInstall installer(GetTestFile("test.apex"));
  if (!installer.Prepare()) {
    return;
  }

  bool success;
  android::binder::Status st =
      service_->stagePackage(installer.test_file, &success);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
  ASSERT_TRUE(success);
}

TEST_F(ApexServiceTest, Activate) {
  PrepareTestApexForInstall installer(GetTestFile("test.apex"));
  if (!installer.Prepare()) {
    return;
  }

  {
    // Check package is not active.
    StatusOr<bool> active = IsActive(installer.package, installer.version);
    ASSERT_TRUE(active.Ok());
    ASSERT_FALSE(*active);
  }

  {
    bool success;
    android::binder::Status st =
        service_->stagePackage(installer.test_file, &success);
    ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
    ASSERT_TRUE(success);
  }

  android::binder::Status st =
      service_->activatePackage(installer.test_installed_file);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();

  {
    // Check package is active.
    StatusOr<bool> active = IsActive(installer.package, installer.version);
    ASSERT_TRUE(active.Ok());
    ASSERT_TRUE(*active) << android::base::Join(GetActivePackagesStrings(),
                                                ',');
  }

  auto cleanup = [&]() {
    // Cleanup.
    st = service_->deactivatePackage(installer.test_installed_file);
    EXPECT_TRUE(st.isOk()) << st.toString8().c_str();
    {
      // Check package is not active.
      StatusOr<bool> active = IsActive(installer.package, installer.version);
      EXPECT_TRUE(active.Ok());
      if (active.Ok()) {
        EXPECT_FALSE(*active)
            << android::base::Join(GetActivePackagesStrings(), ',');
      }
    }

    // TODO: Uninstall.
  };
  auto scope_guard = android::base::make_scope_guard(cleanup);

  {
    // Check that the "latest" view exists.
    std::string latest_path = std::string(kApexRoot) + "/" + installer.package;
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

    std::string versioned_path = std::string(kApexRoot) + "/" +
                                 installer.package + "@" +
                                 std::to_string(installer.version);
    std::vector<std::string> versioned_folder_entries =
        collect_entries_fn(versioned_path);
    std::vector<std::string> latest_folder_entries =
        collect_entries_fn(latest_path);

    EXPECT_TRUE(versioned_folder_entries == latest_folder_entries)
        << "Versioned: " << android::base::Join(versioned_folder_entries, ',')
        << " Latest:" << android::base::Join(latest_folder_entries, ',');
  }

  // Cleanup through ScopeGuard.
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
