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
#include <fstream>
#include <memory>
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
#include "apexd_private.h"
#include "apexd_utils.h"
#include "status_or.h"

namespace android {
namespace apex {

using android::sp;
using android::String16;
using android::base::Join;

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
    return android::base::GetExecutableDirectory();
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

  StatusOr<std::vector<ApexInfo>> GetActivePackages() {
    std::vector<ApexInfo> list;
    android::binder::Status status = service_->getActivePackages(&list);
    if (status.isOk()) {
      return StatusOr<std::vector<ApexInfo>>(list);
    }

    return StatusOr<std::vector<ApexInfo>>::MakeError(
        status.toString8().c_str());
  }

  StatusOr<ApexInfo> GetActivePackage(const std::string& name) {
    ApexInfo package;
    android::binder::Status status = service_->getActivePackage(name, &package);
    if (status.isOk()) {
      return StatusOr<ApexInfo>(package);
    }

    return StatusOr<ApexInfo>::MakeError(status.toString8().c_str());
  }

  std::vector<std::string> GetActivePackagesStrings() {
    std::vector<ApexInfo> list;
    android::binder::Status status = service_->getActivePackages(&list);
    if (status.isOk()) {
      std::vector<std::string> ret;
      for (const ApexInfo& p : list) {
        ret.push_back(p.packageName + "@" + std::to_string(p.versionCode) +
                      " [path=" + p.packagePath + "]");
      }
      return ret;
    }

    std::vector<std::string> error;
    error.push_back("ERROR");
    return error;
  }

  static std::vector<std::string> ListDir(const std::string& path) {
    std::vector<std::string> ret;
    auto d =
        std::unique_ptr<DIR, int (*)(DIR*)>(opendir(path.c_str()), closedir);
    if (d == nullptr) {
      return ret;
    }

    struct dirent* dp;
    while ((dp = readdir(d.get())) != nullptr) {
      std::string tmp;
      switch (dp->d_type) {
        case DT_DIR:
          tmp = "[dir]";
          break;
        case DT_LNK:
          tmp = "[lnk]";
          break;
        case DT_REG:
          tmp = "[reg]";
          break;
        default:
          tmp = "[other]";
          break;
      }
      tmp = tmp.append(dp->d_name);
      ret.push_back(tmp);
    }
    std::sort(ret.begin(), ret.end());
    return ret;
  }

  static std::string GetLogcat() {
    // For simplicity, log to file and read it.
    std::string file = GetTestFile("logcat.tmp.txt");
    std::vector<std::string> args{
        "/system/bin/logcat",
        "-d",
        "-f",
        file,
    };
    std::string error_msg;
    int res = ForkAndRun(args, &error_msg);
    CHECK_EQ(0, res) << error_msg;

    std::string data;
    CHECK(android::base::ReadFileToString(file, &data));

    unlink(file.c_str());

    return data;
  }

  struct PrepareTestApexForInstall {
    static constexpr const char* kTestDir = "/data/staging/apexservice_tmp";

    // This is given to the constructor.
    std::string test_input;  // Original test file.
    std::string selinux_label_input;  // SELinux label to apply.
    std::string test_dir_input;

    // This is derived from the input.
    std::string test_file;            // Prepared path. Under test_dir_input.
    std::string test_installed_file;  // Where apexd will store it.

    std::string package;  // APEX package name.
    uint64_t version;     // APEX version

    explicit PrepareTestApexForInstall(
        const std::string& test,
        const std::string& test_dir = std::string(kTestDir),
        const std::string& selinux_label = "apex_data_file") {
      test_input = test;
      selinux_label_input = selinux_label;
      test_dir_input = test_dir;

      test_file = test_dir_input + "/" + android::base::Basename(test);

      package = "";  // Explicitly mark as not initialized.

      StatusOr<ApexFile> apex_file = ApexFile::Open(test);
      if (!apex_file.Ok()) {
        return;
      }

      const ApexManifest& manifest = apex_file->GetManifest();
      package = manifest.name();
      version = manifest.version();

      test_installed_file = std::string(kApexPackageDataDir) + "/" + package +
                            "@" + std::to_string(version) + ".apex";
    }

    bool Prepare() {
      if (package.empty()) {
        // Failure in constructor. Redo work to get error message.
        auto fail_fn = [&]() {
          StatusOr<ApexFile> apex_file = ApexFile::Open(test_input);
          ASSERT_FALSE(apex_file.Ok());
          ASSERT_TRUE(apex_file.Ok())
              << test_input << " failed to load: " << apex_file.ErrorMessage();
        };
        fail_fn();
        return false;
      }

      auto prepare = [](const std::string& src, const std::string& trg,
                        const std::string& selinux_label) {
        ASSERT_EQ(0, access(src.c_str(), F_OK))
            << src << ": " << strerror(errno);
        const std::string trg_dir = android::base::Dirname(trg);
        if (0 != mkdir(trg_dir.c_str(), 0777)) {
          int saved_errno = errno;
          ASSERT_EQ(saved_errno, EEXIST) << trg << ":" << strerror(saved_errno);
        }

        // Do not use a hardlink, even though it's the simplest solution.
        // b/119569101.
        {
          std::ifstream src_stream(src, std::ios::binary);
          ASSERT_TRUE(src_stream.good());
          std::ofstream trg_stream(trg, std::ios::binary);
          ASSERT_TRUE(trg_stream.good());

          trg_stream << src_stream.rdbuf();
        }

        ASSERT_EQ(0, chmod(trg.c_str(), 0666)) << strerror(errno);
        struct group* g = getgrnam("system");
        ASSERT_NE(nullptr, g);
        ASSERT_EQ(0, chown(trg.c_str(), /* root uid */ 0, g->gr_gid))
            << strerror(errno);

        int rc = setfilecon(
            trg_dir.c_str(),
            std::string("u:object_r:" + selinux_label + ":s0").c_str());
        ASSERT_TRUE(0 == rc || !HaveSelinux()) << strerror(errno);
        rc = setfilecon(
            trg.c_str(),
            std::string("u:object_r:" + selinux_label + ":s0").c_str());
        ASSERT_TRUE(0 == rc || !HaveSelinux()) << strerror(errno);
      };
      prepare(test_input, test_file, selinux_label_input);
      return !HasFatalFailure();
    }

    ~PrepareTestApexForInstall() {
      if (unlink(test_file.c_str()) != 0) {
        PLOG(ERROR) << "Unable to unlink " << test_file;
      }
      if (rmdir(test_dir_input.c_str()) != 0) {
        PLOG(ERROR) << "Unable to rmdir " << test_dir_input;
      }

      if (!package.empty()) {
        // For cleanliness, also attempt to delete apexd's file.
        // TODO: to the unstaging using APIs
        if (unlink(test_installed_file.c_str()) != 0) {
          PLOG(ERROR) << "Unable to unlink " << test_installed_file;
        }
      }
    }
  };

  std::string GetDebugStr(PrepareTestApexForInstall* installer) {
    StringLog log;

    if (installer != nullptr) {
      log << "test_input=" << installer->test_input << " ";
      log << "test_file=" << installer->test_file << " ";
      log << "test_installed_file=" << installer->test_installed_file << " ";
      log << "package=" << installer->package << " ";
      log << "version=" << installer->version << " ";
    }

    log << "active=[" << Join(GetActivePackagesStrings(), ',') << "] ";
    log << kApexPackageDataDir << "=["
        << Join(ListDir(kApexPackageDataDir), ',') << "] ";
    log << kApexRoot << "=[" << Join(ListDir(kApexRoot), ',') << "]";

    return log;
  }

  sp<IApexService> service_;
};

namespace {

bool RegularFileExists(const std::string& path) {
  struct stat buf;
  if (0 != stat(path.c_str(), &buf)) {
    return false;
  }
  return S_ISREG(buf.st_mode);
}

}  // namespace

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
  std::string orig_test_file = GetTestFile("apex.apexd_test.apex");
  std::string test_file = orig_test_file + ".2";
  ASSERT_EQ(0, link(orig_test_file.c_str(), test_file.c_str()))
      << strerror(errno);
  struct Deleter {
    std::string to_delete;
    explicit Deleter(const std::string& t) : to_delete(t) {}
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

TEST_F(ApexServiceTest, StageFailKey) {
  PrepareTestApexForInstall installer(
      GetTestFile("apex.apexd_test_no_inst_key.apex"));
  if (!installer.Prepare()) {
    return;
  }
  ASSERT_EQ(std::string("com.android.apex.test_package.no_inst_key"),
            installer.package);

  bool success;
  android::binder::Status st =
      service_->stagePackage(installer.test_file, &success);
  ASSERT_FALSE(st.isOk());

  // May contain one of two errors.
  std::string error = st.toString8().c_str();

  constexpr const char* kExpectedError1 = "Failed to get realpath of ";
  const size_t pos1 = error.find(kExpectedError1);
  constexpr const char* kExpectedError2 =
      "/etc/security/apex/com.android.apex.test_package.no_inst_key";
  const size_t pos2 = error.find(kExpectedError2);

  constexpr const char* kExpectedError3 =
      "Error verifying "
      "/data/staging/apexservice_tmp/apex.apexd_test_no_inst_key.apex: "
      "couldn't verify public key: Failed to compare the bundled public key "
      "with key";
  const size_t pos3 = error.find(kExpectedError3);

  const size_t npos = std::string::npos;
  EXPECT_TRUE((pos1 != npos && pos2 != npos) || pos3 != npos) << error;
}

TEST_F(ApexServiceTest, StageSuccess) {
  PrepareTestApexForInstall installer(GetTestFile("apex.apexd_test.apex"));
  if (!installer.Prepare()) {
    return;
  }
  ASSERT_EQ(std::string("com.android.apex.test_package"), installer.package);

  bool success;
  android::binder::Status st =
      service_->stagePackage(installer.test_file, &success);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
  ASSERT_TRUE(success);
  EXPECT_TRUE(RegularFileExists(installer.test_installed_file));
}

TEST_F(ApexServiceTest, MultiStageSuccess) {
  PrepareTestApexForInstall installer(GetTestFile("apex.apexd_test.apex"));
  if (!installer.Prepare()) {
    return;
  }
  ASSERT_EQ(std::string("com.android.apex.test_package"), installer.package);

  // TODO: Add second test. Right now, just use a separate version.
  PrepareTestApexForInstall installer2(GetTestFile("apex.apexd_test_v2.apex"));
  if (!installer2.Prepare()) {
    return;
  }
  ASSERT_EQ(std::string("com.android.apex.test_package"), installer2.package);

  std::vector<std::string> packages;
  packages.push_back(installer.test_file);
  packages.push_back(installer2.test_file);

  bool success;
  android::binder::Status st = service_->stagePackages(packages, &success);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
  ASSERT_TRUE(success);
  EXPECT_TRUE(RegularFileExists(installer.test_installed_file));
  EXPECT_TRUE(RegularFileExists(installer2.test_installed_file));
}

template <typename NameProvider>
class ApexServiceActivationTest : public ApexServiceTest {
 public:
  void SetUp() override {
    ApexServiceTest::SetUp();
    ASSERT_NE(nullptr, service_.get());

    installer_ = std::make_unique<PrepareTestApexForInstall>(
        GetTestFile(NameProvider::GetTestName()));
    if (!installer_->Prepare()) {
      return;
    }
    ASSERT_EQ(NameProvider::GetPackageName(), installer_->package);

    {
      // Check package is not active.
      StatusOr<bool> active =
          IsActive(installer_->package, installer_->version);
      ASSERT_TRUE(active.Ok());
      ASSERT_FALSE(*active);
    }

    {
      bool success;
      android::binder::Status st =
          service_->stagePackage(installer_->test_file, &success);
      ASSERT_TRUE(st.isOk()) << st.toString8().c_str();
      ASSERT_TRUE(success);
    }
  }

  void TearDown() override {
    // Attempt to deactivate.
    if (installer_ != nullptr) {
      service_->deactivatePackage(installer_->test_installed_file);
    }

    installer_.reset();
  }

  std::unique_ptr<PrepareTestApexForInstall> installer_;
};

struct SuccessNameProvider {
  static std::string GetTestName() { return "apex.apexd_test.apex"; }
  static std::string GetPackageName() {
    return "com.android.apex.test_package";
  }
};

class ApexServiceActivationSuccessTest
    : public ApexServiceActivationTest<SuccessNameProvider> {};

TEST_F(ApexServiceActivationSuccessTest, Activate) {
  android::binder::Status st =
      service_->activatePackage(installer_->test_installed_file);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str() << " "
                         << GetDebugStr(installer_.get());

  {
    // Check package is active.
    StatusOr<bool> active = IsActive(installer_->package, installer_->version);
    ASSERT_TRUE(active.Ok());
    ASSERT_TRUE(*active) << Join(GetActivePackagesStrings(), ',');
  }

  {
    // Check that the "latest" view exists.
    std::string latest_path =
        std::string(kApexRoot) + "/" + installer_->package;
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
                                 installer_->package + "@" +
                                 std::to_string(installer_->version);
    std::vector<std::string> versioned_folder_entries =
        collect_entries_fn(versioned_path);
    std::vector<std::string> latest_folder_entries =
        collect_entries_fn(latest_path);

    EXPECT_TRUE(versioned_folder_entries == latest_folder_entries)
        << "Versioned: " << Join(versioned_folder_entries, ',')
        << " Latest: " << Join(latest_folder_entries, ',');
  }
}

TEST_F(ApexServiceActivationSuccessTest, GetActivePackages) {
  android::binder::Status st =
      service_->activatePackage(installer_->test_installed_file);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str() << " "
                         << GetDebugStr(installer_.get());

  StatusOr<std::vector<ApexInfo>> active = GetActivePackages();
  ASSERT_TRUE(active.Ok());
  ApexInfo match;

  for (ApexInfo info : *active) {
    if (info.packageName == installer_->package) {
      match = info;
      break;
    }
  }

  ASSERT_EQ(installer_->package, match.packageName);
  ASSERT_EQ(installer_->version, static_cast<uint64_t>(match.versionCode));
  ASSERT_EQ(installer_->test_installed_file, match.packagePath);
}

TEST_F(ApexServiceActivationSuccessTest, GetActivePackage) {
  android::binder::Status st =
      service_->activatePackage(installer_->test_installed_file);
  ASSERT_TRUE(st.isOk()) << st.toString8().c_str() << " "
                         << GetDebugStr(installer_.get());

  StatusOr<ApexInfo> active = GetActivePackage(installer_->package);
  ASSERT_TRUE(active.Ok());

  ASSERT_EQ(installer_->package, active->packageName);
  ASSERT_EQ(installer_->version, static_cast<uint64_t>(active->versionCode));
  ASSERT_EQ(installer_->test_installed_file, active->packagePath);
}

class ApexServicePrePostInstallTest : public ApexServiceTest {
 public:
  template <typename Fn>
  void RunPrePost(Fn fn, const std::vector<std::string>& apex_names,
                  const char* test_message) {
    int old_selinux_state = security_getenforce();
    if (old_selinux_state == -1) {
      LOG(ERROR) << "Could not determine selinux enforcement";
    } else if (old_selinux_state == 1) {
      int res = security_setenforce(0);
      if (res != 0) {
        LOG(ERROR) << "Unable to disable selinux enforcement";
      }
    }
    auto scope_guard = android::base::make_scope_guard([&]() {
      if (old_selinux_state != -1 &&
          security_setenforce(old_selinux_state) != 0) {
        LOG(ERROR) << "Unable to reset selinux enforcement to "
                   << old_selinux_state;
      }
    });
    // Using unique_ptr is just the easiest here.
    using InstallerUPtr = std::unique_ptr<PrepareTestApexForInstall>;
    std::vector<InstallerUPtr> installers;
    std::vector<std::string> pkgs;

    for (const std::string& apex_name : apex_names) {
      InstallerUPtr installer(
          new PrepareTestApexForInstall(GetTestFile(apex_name)));
      if (!installer->Prepare()) {
        return;
      }
      pkgs.push_back(installer->test_file);
      installers.emplace_back(std::move(installer));
    }
    android::binder::Status st = (service_.get()->*fn)(pkgs);
    ASSERT_TRUE(st.isOk()) << st.toString8().c_str();

    std::string logcat = GetLogcat();
    EXPECT_NE(std::string::npos, logcat.find(test_message)) << logcat;

    // Ensure that the package is neither active nor mounted.
    for (const InstallerUPtr& installer : installers) {
      StatusOr<bool> active = IsActive(installer->package, installer->version);
      ASSERT_TRUE(active.Ok());
      EXPECT_FALSE(*active);
    }
    for (const InstallerUPtr& installer : installers) {
      StatusOr<ApexFile> apex = ApexFile::Open(installer->test_input);
      ASSERT_TRUE(apex.Ok());
      std::string path =
          apexd_private::GetPackageMountPoint(apex->GetManifest());
      std::string entry = std::string("[dir]").append(path);
      std::vector<std::string> slash_apex = ListDir(kApexRoot);
      auto it = std::find(slash_apex.begin(), slash_apex.end(), entry);
      EXPECT_TRUE(it == slash_apex.end()) << Join(slash_apex, ',');
    }
  }
};

TEST_F(ApexServicePrePostInstallTest, Preinstall) {
  RunPrePost(&IApexService::preinstallPackages,
             {"apex.apexd_test_preinstall.apex"}, "sh      : PreInstall Test");
}

TEST_F(ApexServicePrePostInstallTest, MultiPreinstall) {
  constexpr const char* kLogcatText =
      "sh      : /apex/com.android.apex.test_package/etc/sample_prebuilt_file";
  RunPrePost(&IApexService::preinstallPackages,
             {"apex.apexd_test_preinstall.apex", "apex.apexd_test.apex"},
             kLogcatText);
}

TEST_F(ApexServicePrePostInstallTest, Postinstall) {
  RunPrePost(&IApexService::postinstallPackages,
             {"apex.apexd_test_postinstall.apex"},
             "sh      : PostInstall Test");
}

TEST_F(ApexServicePrePostInstallTest, MultiPostinstall) {
  constexpr const char* kLogcatText =
      "sh      : /apex/com.android.apex.test_package/etc/sample_prebuilt_file";
  RunPrePost(&IApexService::postinstallPackages,
             {"apex.apexd_test_postinstall.apex", "apex.apexd_test.apex"},
             kLogcatText);
}

TEST_F(ApexServiceTest, SubmitSingleSessionTestSuccess) {
  PrepareTestApexForInstall installer(GetTestFile("apex.apexd_test.apex"),
                                      "/data/staging/session_123",
                                      "staging_data_file");
  if (!installer.Prepare()) {
    FAIL() << GetDebugStr(&installer);
  }

  ApexInfoList list;
  bool ret_value;
  std::vector<int> empty_child_session_ids;
  android::binder::Status status = service_->submitStagedSession(
      123, empty_child_session_ids, &list, &ret_value);

  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_TRUE(ret_value);
  EXPECT_EQ(1u, list.apexInfos.size());
  ApexInfo match;
  for (ApexInfo info : list.apexInfos) {
    if (info.packageName == installer.package) {
      match = info;
      break;
    }
  }

  ASSERT_EQ(installer.package, match.packageName);
  ASSERT_EQ(installer.version, static_cast<uint64_t>(match.versionCode));
  ASSERT_EQ(installer.test_file, match.packagePath);

  ApexSessionInfo session;
  status = service_->getStagedSessionInfo(123, &session);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_FALSE(session.isUnknown);
  EXPECT_TRUE(session.isVerified);
  EXPECT_FALSE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);

  status = service_->markStagedSessionReady(123, &ret_value);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  ASSERT_TRUE(ret_value);

  status = service_->getStagedSessionInfo(123, &session);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_FALSE(session.isUnknown);
  EXPECT_FALSE(session.isVerified);
  EXPECT_TRUE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);

  // Call markStagedSessionReady again. Should be a no-op.
  status = service_->markStagedSessionReady(123, &ret_value);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  ASSERT_TRUE(ret_value);

  status = service_->getStagedSessionInfo(123, &session);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_FALSE(session.isUnknown);
  EXPECT_FALSE(session.isVerified);
  EXPECT_TRUE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);
}

TEST_F(ApexServiceTest, SubmitSingleSessionTestFail) {
  PrepareTestApexForInstall installer(
      GetTestFile("apex.apexd_test_no_inst_key.apex"),
      "/data/staging/session_456", "staging_data_file");
  if (!installer.Prepare()) {
    FAIL() << GetDebugStr(&installer);
  }

  ApexInfoList list;
  bool ret_value;
  std::vector<int> empty_child_session_ids;
  android::binder::Status status = service_->submitStagedSession(
      456, empty_child_session_ids, &list, &ret_value);

  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_FALSE(ret_value);

  ApexSessionInfo session;
  status = service_->getStagedSessionInfo(456, &session);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_TRUE(session.isUnknown);
  EXPECT_FALSE(session.isVerified);
  EXPECT_FALSE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);
}

TEST_F(ApexServiceTest, SubmitMultiSessionTestSuccess) {
  // Parent session id: 10
  // Children session ids: 20 30
  PrepareTestApexForInstall installer(GetTestFile("apex.apexd_test.apex"),
                                      "/data/staging/session_20",
                                      "staging_data_file");
  PrepareTestApexForInstall installer2(
      GetTestFile("apex.apexd_test_different_app.apex"),
      "/data/staging/session_30", "staging_data_file");
  if (!installer.Prepare() || !installer2.Prepare()) {
    FAIL() << GetDebugStr(&installer) << GetDebugStr(&installer2);
  }

  ApexInfoList list;
  bool ret_value;
  std::vector<int> child_session_ids = {20, 30};
  android::binder::Status status =
      service_->submitStagedSession(10, child_session_ids, &list, &ret_value);

  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  ASSERT_TRUE(ret_value);
  EXPECT_EQ(2u, list.apexInfos.size());
  ApexInfo match;
  bool package1_found = false;
  bool package2_found = false;
  for (ApexInfo info : list.apexInfos) {
    if (info.packageName == installer.package) {
      ASSERT_EQ(installer.package, info.packageName);
      ASSERT_EQ(installer.version, static_cast<uint64_t>(info.versionCode));
      ASSERT_EQ(installer.test_file, info.packagePath);
      package1_found = true;
    } else if (info.packageName == installer2.package) {
      ASSERT_EQ(installer2.package, info.packageName);
      ASSERT_EQ(installer2.version, static_cast<uint64_t>(info.versionCode));
      ASSERT_EQ(installer2.test_file, info.packagePath);
      package2_found = true;
    } else {
      FAIL() << "Unexpected package found " << info.packageName
             << GetDebugStr(&installer) << GetDebugStr(&installer2);
    }
  }
  ASSERT_TRUE(package1_found);
  ASSERT_TRUE(package2_found);

  ApexSessionInfo session;
  status = service_->getStagedSessionInfo(10, &session);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_FALSE(session.isUnknown);
  EXPECT_TRUE(session.isVerified);
  EXPECT_FALSE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);

  status = service_->markStagedSessionReady(10, &ret_value);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  ASSERT_TRUE(ret_value);

  status = service_->getStagedSessionInfo(10, &session);
  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  EXPECT_FALSE(session.isUnknown);
  EXPECT_FALSE(session.isVerified);
  EXPECT_TRUE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);
}

TEST_F(ApexServiceTest, SubmitMultiSessionTestFail) {
  // Parent session id: 11
  // Children session ids: 21 31
  PrepareTestApexForInstall installer(GetTestFile("apex.apexd_test.apex"),
                                      "/data/staging/session_21",
                                      "staging_data_file");
  PrepareTestApexForInstall installer2(
      GetTestFile("apex.apexd_test_no_inst_key.apex"),
      "/data/staging/session_31", "staging_data_file");
  if (!installer.Prepare() || !installer2.Prepare()) {
    FAIL() << GetDebugStr(&installer) << GetDebugStr(&installer2);
  }
  ApexInfoList list;
  bool ret_value;
  std::vector<int> child_session_ids = {21, 31};
  android::binder::Status status =
      service_->submitStagedSession(11, child_session_ids, &list, &ret_value);

  ASSERT_TRUE(status.isOk())
      << status.toString8().c_str() << " " << GetDebugStr(&installer);
  ASSERT_FALSE(ret_value);
}

TEST_F(ApexServiceTest, MarkStagedSessionReadyFail) {
  // We should fail if we ask information about a session we don't know.
  bool ret_value;
  auto status = service_->markStagedSessionReady(666, &ret_value);
  ASSERT_TRUE(status.isOk()) << status.toString8().c_str();
  ASSERT_FALSE(ret_value);

  ApexSessionInfo session;
  status = service_->getStagedSessionInfo(666, &session);
  ASSERT_TRUE(status.isOk()) << status.toString8().c_str();
  EXPECT_TRUE(session.isUnknown);
  EXPECT_FALSE(session.isVerified);
  EXPECT_FALSE(session.isStaged);
  EXPECT_FALSE(session.isActivated);
  EXPECT_FALSE(session.isActivationPendingRetry);
  EXPECT_FALSE(session.isActivationFailed);
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
