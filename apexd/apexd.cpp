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

#define LOG_TAG "apexd"

#include "apex_file.h"
#include "apex_manifest.h"
#include "apexservice.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory>
#include <string>

#include <utils/Errors.h>

using android::defaultServiceManager;
using android::IPCThreadState;
using android::ProcessState;
using android::sp;
using android::String16;
using android::apex::ApexService;
using android::base::Basename;
using android::base::EndsWith;
using android::base::StringPrintf;
using android::base::unique_fd;

using std::string;

namespace android {
namespace apex {

static constexpr const char* kApexPackageDir = "/data/apex";
static constexpr const char* kApexPackageSuffix = ".apex";
static constexpr const char* kApexRoot = "/mnt/apex";
static constexpr const char* kApexLoopIdPrefix = "apex:";

status_t createLoopDevice(const std::string& target, const int32_t imageOffset,
                          const size_t imageSize, string& out_device) {
  unique_fd ctl_fd(open("/dev/loop-control", O_RDWR | O_CLOEXEC));
  if (ctl_fd.get() == -1) {
    PLOG(ERROR) << "Failed to open loop-control";
    return -errno;
  }

  int num = ioctl(ctl_fd.get(), LOOP_CTL_GET_FREE);
  if (num == -1) {
    PLOG(ERROR) << "Failed LOOP_CTL_GET_FREE";
    return -errno;
  }

  out_device = StringPrintf("/dev/block/loop%d", num);

  unique_fd target_fd(open(target.c_str(), O_RDWR | O_CLOEXEC));
  if (target_fd.get() == -1) {
    PLOG(ERROR) << "Failed to open " << target;
    return -errno;
  }
  unique_fd device_fd(open(out_device.c_str(), O_RDWR | O_CLOEXEC));
  if (device_fd.get() == -1) {
    PLOG(ERROR) << "Failed to open " << out_device;
    return -errno;
  }

  if (ioctl(device_fd.get(), LOOP_SET_FD, target_fd.get()) == -1) {
    PLOG(ERROR) << "Failed to LOOP_SET_FD";
    return -errno;
  }

  struct loop_info64 li;
  memset(&li, 0, sizeof(li));
  strlcpy((char*)li.lo_crypt_name, kApexLoopIdPrefix, LO_NAME_SIZE);
  li.lo_offset = imageOffset;
  li.lo_sizelimit = imageSize;
  if (ioctl(device_fd.get(), LOOP_SET_STATUS64, &li) == -1) {
    PLOG(ERROR) << "Failed to LOOP_SET_STATUS64";
    return -errno;
  }

  return 0;
}

void destroyAllLoopDevices() {
  std::string root = "/dev/block/";
  auto dirp =
      std::unique_ptr<DIR, int (*)(DIR*)>(opendir(root.c_str()), closedir);
  if (!dirp) {
    PLOG(ERROR) << "Failed to open /dev/block/, can't destroy loop devices.";
    return;
  }

  // Poke through all devices looking for loop devices.
  struct dirent* de;
  while ((de = readdir(dirp.get()))) {
    auto test = std::string(de->d_name);
    if (!android::base::StartsWith(test, "loop")) continue;

    auto path = root + de->d_name;
    unique_fd fd(open(path.c_str(), O_RDWR | O_CLOEXEC));
    if (fd.get() == -1) {
      if (errno != ENOENT) {
        PLOG(WARNING) << "Failed to open " << path;
      }
      continue;
    }

    struct loop_info64 li;
    if (ioctl(fd.get(), LOOP_GET_STATUS64, &li) < 0) {
      PLOG(WARNING) << "Failed to LOOP_GET_STATUS64 " << path;
      continue;
    }

    auto id = std::string((char*)li.lo_crypt_name);
    if (android::base::StartsWith(id, kApexLoopIdPrefix)) {
      LOG(DEBUG) << "Tearing down stale loop device at " << path << " named "
                 << id;

      if (ioctl(fd.get(), LOOP_CLR_FD, 0) < 0) {
        PLOG(WARNING) << "Failed to LOOP_CLR_FD " << path;
      }
    } else {
      LOG(VERBOSE) << "Found unmanaged loop device at " << path << " named "
                   << id;
    }
  }
}

static constexpr int kLoopDeviceSetupRetries = 3;

static constexpr const char* kApexServiceName = "apexservice";

void installPackage(const string& full_path) {
  LOG(INFO) << "Installing " << full_path;

  std::unique_ptr<ApexFile> apex = ApexFile::Open(full_path);
  if (apex.get() == nullptr) {
    // Error opening the file.
    return;
  }

  std::unique_ptr<ApexManifest> manifest =
      ApexManifest::Open(apex->GetManifest());
  if (manifest.get() == nullptr) {
    // Error parsing manifest.
    return;
  }
  string packageId = manifest->GetName() + "@" +
                     std::to_string(manifest->GetVersion());

  string loopback;
  status_t ret;
  int retries = 0;
  do {
    ret = createLoopDevice(full_path, apex->GetImageOffset(),
                           apex->GetImageSize(), loopback);
    ++retries;
  } while (ret != NO_ERROR && retries < kLoopDeviceSetupRetries);
  if (ret != NO_ERROR) {
    return;
  }
  LOG(VERBOSE) << "Loopback device created: " << loopback;

  string mountPoint = StringPrintf("%s/%s", kApexRoot, packageId.c_str());
  LOG(VERBOSE) << "Creating mount point: " << mountPoint;
  mkdir(mountPoint.c_str(), 0755);

  if (mount(loopback.c_str(), mountPoint.c_str(), "ext4",
            MS_NOATIME | MS_NODEV | MS_NOSUID | MS_DIRSYNC | MS_RDONLY,
            NULL) == 0) {
    LOG(INFO) << "Successfully mounted package " << full_path << " on "
              << mountPoint;
  } else {
    PLOG(ERROR) << "Mounting failed for package " << full_path;
    // Tear down loop device.
    unique_fd fd(open(loopback.c_str(), O_RDWR | O_CLOEXEC));
    if (fd.get() == -1) {
      PLOG(WARNING) << "Failed to open " << loopback
                    << " while attempting to clean up unused loop device";
      return;
    }

    if (ioctl(fd.get(), LOOP_CLR_FD, 0) < 0) {
      PLOG(WARNING) << "Failed to clean up unused loop device " << loopback;
      return;
    }
  }
}

void unmountAndDetachExistingImages() {
  // TODO: this procedure should probably not be needed anymore when apexd
  // becomes an actual daemon. Remove if that's the case.
  LOG(INFO) << "Scanning " << kApexRoot
            << " looking for packages already mounted.";
  auto d = std::unique_ptr<DIR, int (*)(DIR*)>(opendir(kApexRoot), closedir);
  if (!d) {
    // Nothing to do
    PLOG(ERROR) << "Can't open " << kApexRoot;
    return;
  }

  struct dirent* dp;
  while ((dp = readdir(d.get())) != NULL) {
    if (dp->d_type != DT_DIR || (strcmp(dp->d_name, ".") == 0) ||
        (strcmp(dp->d_name, "..") == 0)) {
      continue;
    }
    LOG(INFO) << "Unmounting " << kApexRoot << "/" << dp->d_name;
    // Lazily try to umount whatever is mounted.
    if (umount2(StringPrintf("%s/%s", kApexRoot, dp->d_name).c_str(),
                UMOUNT_NOFOLLOW | MNT_DETACH) != 0 &&
        errno != EINVAL && errno != ENOENT) {
      PLOG(ERROR) << "Failed to unmount directory " << kApexRoot << "/"
                  << dp->d_name;
    }
  }

  destroyAllLoopDevices();
}

void setupApexRoot() {
  LOG(INFO) << "Creating APEX mount point at " << kApexRoot;
  mkdir(kApexRoot, 0755);
}

void scanPackagesDirAndMount() {
  LOG(INFO) << "Scanning " << kApexPackageDir << " looking for APEX packages.";
  auto d =
      std::unique_ptr<DIR, int (*)(DIR*)>(opendir(kApexPackageDir), closedir);

  if (!d) {
    LOG(WARNING) << "Package directory " << kApexPackageDir
                 << " not found, nothing to do.";
    return;
  }
  struct dirent* dp;
  while ((dp = readdir(d.get())) != NULL) {
    if (dp->d_type != DT_REG || !EndsWith(dp->d_name, kApexPackageSuffix)) {
      continue;
    }
    LOG(INFO) << "Found " << dp->d_name;

    installPackage(StringPrintf("%s/%s", kApexPackageDir, dp->d_name));
  }
}
}  // namespace apex
}  // namespace android

int main(int /*argc*/, char** /*argv*/) {
  sp<ProcessState> ps(ProcessState::self());

  // TODO: add a -v flag or an external setting to change LogSeverity.
  android::base::SetMinimumLogSeverity(android::base::VERBOSE);

  // Create binder service and register with servicemanager
  sp<ApexService> apexService = new ApexService();
  defaultServiceManager()->addService(String16(android::apex::kApexServiceName),
                                      apexService);

  android::apex::unmountAndDetachExistingImages();
  android::apex::setupApexRoot();
  android::apex::scanPackagesDirAndMount();

  // Start threadpool, wait for IPC
  ps->startThreadPool();
  IPCThreadState::self()->joinThreadPool();  // should not return

  return 1;
}
