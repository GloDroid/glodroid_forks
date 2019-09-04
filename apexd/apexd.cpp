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

#include "apexd.h"
#include "apexd_private.h"

#include "apex_database.h"
#include "apex_file.h"
#include "apex_key.h"
#include "apex_manifest.h"
#include "apex_shim.h"
#include "apexd_checkpoint.h"
#include "apexd_loop.h"
#include "apexd_prepostinstall.h"
#include "apexd_prop.h"
#include "apexd_session.h"
#include "apexd_utils.h"
#include "apexd_verity.h"
#include "string_log.h"

#include <ApexProperties.sysprop.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/properties.h>
#include <android-base/scopeguard.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <libavb/libavb.h>
#include <libdm/dm.h>
#include <libdm/dm_table.h>
#include <libdm/dm_target.h>
#include <selinux/android.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using android::base::ErrnoError;
using android::base::Error;
using android::base::Errorf;
using android::base::Join;
using android::base::ReadFully;
using android::base::Result;
using android::base::StartsWith;
using android::base::StringPrintf;
using android::base::unique_fd;
using android::dm::DeviceMapper;
using android::dm::DmDeviceState;
using android::dm::DmTable;
using android::dm::DmTargetVerity;

using apex::proto::SessionState;

namespace android {
namespace apex {

using MountedApexData = MountedApexDatabase::MountedApexData;

namespace {

// These should be in-sync with system/sepolicy/public/property_contexts
static constexpr const char* kApexStatusSysprop = "apexd.status";
static constexpr const char* kApexStatusStarting = "starting";
static constexpr const char* kApexStatusReady = "ready";

static constexpr const char* kApexVerityOnSystemProp =
    "persist.apexd.verity_on_system";
static bool gForceDmVerityOnSystem =
    android::base::GetBoolProperty(kApexVerityOnSystemProp, false);

// This should be in UAPI, but it's not :-(
static constexpr const char* kDmVerityRestartOnCorruption =
    "restart_on_corruption";

MountedApexDatabase gMountedApexes;

CheckpointInterface* gVoldService;
bool gSupportsFsCheckpoints = false;
bool gInFsCheckpointMode = false;

static constexpr size_t kLoopDeviceSetupAttempts = 3u;

static const bool kUpdatable =
    android::sysprop::ApexProperties::updatable().value_or(false);

bool gBootstrap = false;
static const std::vector<const std::string> kBootstrapApexes = {
    "com.android.art",
    "com.android.i18n",
    "com.android.runtime",
    "com.android.tzdata",
};

static constexpr const int kNumRetriesWhenCheckpointingEnabled = 1;

bool isBootstrapApex(const ApexFile& apex) {
  return std::find(kBootstrapApexes.begin(), kBootstrapApexes.end(),
                   apex.GetManifest().name()) != kBootstrapApexes.end();
}

// Pre-allocate loop devices so that we don't have to wait for them
// later when actually activating APEXes.
Result<void> preAllocateLoopDevices() {
  auto scan = FindApexes(kApexPackageBuiltinDirs);
  if (!scan) {
    return scan.error();
  }

  auto size = 0;
  for (const auto& path : *scan) {
    auto apexFile = ApexFile::Open(path);
    if (!apexFile) {
      continue;
    }
    size++;
    // bootstrap Apexes may be activated on separate namespaces.
    if (isBootstrapApex(*apexFile)) {
      size++;
    }
  }

  // note: do not call preAllocateLoopDevices() if size == 0
  // or the device does not support updatable APEX.
  // For devices (e.g. ARC) which doesn't support loop-control
  // preAllocateLoopDevices() can cause problem when it tries
  // to access /dev/loop-control.
  if (size == 0 || !kUpdatable) {
    return {};
  }
  return loop::preAllocateLoopDevices(size);
}

std::unique_ptr<DmTable> createVerityTable(const ApexVerityData& verity_data,
                                           const std::string& block_device,
                                           const std::string& hash_device,
                                           bool restart_on_corruption) {
  AvbHashtreeDescriptor* desc = verity_data.desc.get();
  auto table = std::make_unique<DmTable>();

  uint32_t hash_start_block = 0;
  if (hash_device == block_device) {
    hash_start_block = desc->tree_offset / desc->hash_block_size;
  }

  auto target = std::make_unique<DmTargetVerity>(
      0, desc->image_size / 512, desc->dm_verity_version, block_device,
      hash_device, desc->data_block_size, desc->hash_block_size,
      desc->image_size / desc->data_block_size, hash_start_block,
      verity_data.hash_algorithm, verity_data.root_digest, verity_data.salt);

  target->IgnoreZeroBlocks();
  if (restart_on_corruption) {
    target->SetVerityMode(kDmVerityRestartOnCorruption);
  }
  table->AddTarget(std::move(target));

  table->set_readonly(true);

  return table;
}

enum WaitForDeviceMode {
  kWaitToBeCreated = 0,
  kWaitToBeDeleted,
};

Result<void> waitForDevice(const std::string& device,
                           const WaitForDeviceMode& mode) {
  // TODO(b/122059364): Make this more efficient
  // TODO: use std::chrono?

  // Deleting a device might take more time, so wait a little bit longer.
  size_t num_tries = mode == kWaitToBeCreated ? 10u : 15u;

  LOG(DEBUG) << "Waiting for " << device << " to be "
             << (mode == kWaitToBeCreated ? "created" : " deleted");
  for (size_t i = 0; i < num_tries; ++i) {
    Result<bool> status = PathExists(device);
    if (status) {
      if (mode == kWaitToBeCreated && *status) {
        return {};
      }
      if (mode == kWaitToBeDeleted && !*status) {
        return {};
      }
    }
    if (i + 1 < num_tries) {
      usleep(50000);
    }
  }

  return Error() << "Failed to wait for device " << device << " to be "
                 << (mode == kWaitToBeCreated ? " created" : " deleted");
}

// Deletes a dm-verity device with a given name and path.
// Synchronizes on the device actually being deleted from userspace.
Result<void> DeleteVerityDevice(const std::string& name,
                                const std::string& path) {
  DeviceMapper& dm = DeviceMapper::Instance();
  if (!dm.DeleteDevice(name)) {
    return Error() << "Failed to delete device " << name << " with path "
                   << path;
  }
  // Block until device is deleted from userspace.
  return waitForDevice(path, kWaitToBeDeleted);
}

// Deletes dm-verity device with a given name.
// See function above.
Result<void> DeleteVerityDevice(const std::string& name) {
  DeviceMapper& dm = DeviceMapper::Instance();
  std::string path;
  if (!dm.GetDmDevicePathByName(name, &path)) {
    return Error() << "Unable to get path for dm-verity device " << name;
  }
  return DeleteVerityDevice(name, path);
}

class DmVerityDevice {
 public:
  DmVerityDevice() : cleared_(true) {}
  explicit DmVerityDevice(std::string name)
      : name_(std::move(name)), cleared_(false) {}
  DmVerityDevice(std::string name, std::string dev_path)
      : name_(std::move(name)),
        dev_path_(std::move(dev_path)),
        cleared_(false) {}

  DmVerityDevice(DmVerityDevice&& other) noexcept
      : name_(std::move(other.name_)),
        dev_path_(std::move(other.dev_path_)),
        cleared_(other.cleared_) {
    other.cleared_ = true;
  }

  DmVerityDevice& operator=(DmVerityDevice&& other) noexcept {
    name_ = other.name_;
    dev_path_ = other.dev_path_;
    cleared_ = other.cleared_;
    other.cleared_ = true;
    return *this;
  }

  ~DmVerityDevice() {
    if (!cleared_) {
      Result<void> ret = DeleteVerityDevice(name_, dev_path_);
      if (!ret) {
        LOG(ERROR) << ret.error();
      }
    }
  }

  const std::string& GetName() const { return name_; }
  const std::string& GetDevPath() const { return dev_path_; }
  void SetDevPath(const std::string& dev_path) { dev_path_ = dev_path; }

  void Release() { cleared_ = true; }

 private:
  std::string name_;
  std::string dev_path_;
  bool cleared_;
};

Result<DmVerityDevice> createVerityDevice(const std::string& name,
                                          const DmTable& table) {
  DeviceMapper& dm = DeviceMapper::Instance();

  if (dm.GetState(name) != DmDeviceState::INVALID) {
    // TODO: since apexd tears down devices during unmount, can this happen?
    LOG(WARNING) << "Deleting existing dm device " << name;
    const Result<void>& status = DeleteVerityDevice(name);
    if (!status) {
      // TODO: should we fail instead?
      LOG(ERROR) << "Failed to delete device " << name << " : "
                 << status.error();
    }
  }

  if (!dm.CreateDevice(name, table)) {
    return Errorf("Couldn't create verity device.");
  }
  DmVerityDevice dev(name);

  std::string dev_path;
  if (!dm.GetDmDevicePathByName(name, &dev_path)) {
    return Errorf("Couldn't get verity device path!");
  }
  dev.SetDevPath(dev_path);

  return dev;
}

Result<void> RemovePreviouslyActiveApexFiles(
    const std::unordered_set<std::string>& affected_packages,
    const std::unordered_set<std::string>& files_to_keep) {
  auto all_active_apex_files = FindApexFilesByName(kActiveApexPackagesDataDir);

  if (!all_active_apex_files) {
    return all_active_apex_files.error();
  }

  for (const std::string& path : *all_active_apex_files) {
    Result<ApexFile> apex_file = ApexFile::Open(path);
    if (!apex_file) {
      return apex_file.error();
    }

    const std::string& package_name = apex_file->GetManifest().name();
    if (affected_packages.find(package_name) == affected_packages.end()) {
      // This apex belongs to a package that wasn't part of this stage sessions,
      // hence it should be kept.
      continue;
    }

    if (files_to_keep.find(apex_file->GetPath()) != files_to_keep.end()) {
      // This is a path that was staged and should be kept.
      continue;
    }

    LOG(DEBUG) << "Deleting previously active apex " << apex_file->GetPath();
    if (unlink(apex_file->GetPath().c_str()) != 0) {
      return ErrnoError() << "Failed to unlink " << apex_file->GetPath();
    }
  }

  return {};
}

// Reads the entire device to verify the image is authenticatic
Result<void> readVerityDevice(const std::string& verity_device,
                              uint64_t device_size) {
  static constexpr int kBlockSize = 4096;
  static constexpr size_t kBufSize = 1024 * kBlockSize;
  std::vector<uint8_t> buffer(kBufSize);

  unique_fd fd(TEMP_FAILURE_RETRY(open(verity_device.c_str(), O_RDONLY)));
  if (fd.get() == -1) {
    return ErrnoError() << "Can't open " << verity_device;
  }

  size_t bytes_left = device_size;
  while (bytes_left > 0) {
    size_t to_read = std::min(bytes_left, kBufSize);
    if (!android::base::ReadFully(fd.get(), buffer.data(), to_read)) {
      return ErrnoError() << "Can't verify " << verity_device << "; corrupted?";
    }
    bytes_left -= to_read;
  }

  return {};
}

Result<void> VerifyMountedImage(const ApexFile& apex,
                                const std::string& mount_point) {
  auto status = apex.VerifyManifestMatches(mount_point);
  if (!status) {
    return status;
  }
  if (shim::IsShimApex(apex)) {
    return shim::ValidateShimApex(mount_point, apex);
  }
  return {};
}

Result<MountedApexData> MountPackageImpl(const ApexFile& apex,
                                         const std::string& mountPoint,
                                         const std::string& device_name,
                                         bool verifyImage) {
  LOG(VERBOSE) << "Creating mount point: " << mountPoint;
  // Note: the mount point could exist in case when the APEX was activated
  // during the bootstrap phase (e.g., the runtime or tzdata APEX).
  // Although we have separate mount namespaces to separate the early activated
  // APEXes from the normally activate APEXes, the mount points themselves
  // are shared across the two mount namespaces because /apex (a tmpfs) itself
  // mounted at / which is (and has to be) a shared mount. Therefore, if apexd
  // finds an empty directory under /apex, it's not a problem and apexd can use
  // it.
  auto exists = PathExists(mountPoint);
  if (!exists) {
    return exists.error();
  }
  if (!*exists && mkdir(mountPoint.c_str(), kMkdirMode) != 0) {
    return ErrnoError() << "Could not create mount point " << mountPoint;
  }
  auto deleter = [&mountPoint]() {
    if (rmdir(mountPoint.c_str()) != 0) {
      PLOG(WARNING) << "Could not rmdir " << mountPoint;
    }
  };
  auto scope_guard = android::base::make_scope_guard(deleter);
  if (!IsEmptyDirectory(mountPoint)) {
    return ErrnoError() << mountPoint << " is not empty";
  }

  const std::string& full_path = apex.GetPath();

  loop::LoopbackDeviceUniqueFd loopbackDevice;
  for (size_t attempts = 1;; ++attempts) {
    Result<loop::LoopbackDeviceUniqueFd> ret = loop::createLoopDevice(
        full_path, apex.GetImageOffset(), apex.GetImageSize());
    if (ret) {
      loopbackDevice = std::move(*ret);
      break;
    }
    if (attempts >= kLoopDeviceSetupAttempts) {
      return Error() << "Could not create loop device for " << full_path << ": "
                     << ret.error();
    }
  }
  LOG(VERBOSE) << "Loopback device created: " << loopbackDevice.name;

  auto verityData = apex.VerifyApexVerity();
  if (!verityData) {
    return Error() << "Failed to verify Apex Verity data for " << full_path
                   << ": " << verityData.error();
  }
  std::string blockDevice = loopbackDevice.name;
  MountedApexData apex_data(loopbackDevice.name, apex.GetPath(), mountPoint,
                            /* device_name = */ "");

  // for APEXes in immutable partitions, we don't need to mount them on
  // dm-verity because they are already in the dm-verity protected partition;
  // system. However, note that we don't skip verification to ensure that APEXes
  // are correctly signed.
  const bool mountOnVerity =
      gForceDmVerityOnSystem || !isPathForBuiltinApexes(full_path);
  DmVerityDevice verityDev;
  loop::LoopbackDeviceUniqueFd loop_for_hash;
  if (mountOnVerity) {
    std::string hash_device = loopbackDevice.name;
    if (verityData->desc->tree_size == 0) {
      auto hash_tree = GetHashTree(apex, *verityData);
      if (!hash_tree) {
        return hash_tree.error();
      }
      loop_for_hash = std::move(*hash_tree);
      hash_device = loop_for_hash.name;
    }
    auto verityTable =
        createVerityTable(*verityData, loopbackDevice.name, hash_device,
                          /* restart_on_corruption = */ !verifyImage);
    Result<DmVerityDevice> verityDevRes =
        createVerityDevice(device_name, *verityTable);
    if (!verityDevRes) {
      return Error() << "Failed to create Apex Verity device " << full_path
                     << ": " << verityDevRes.error();
    }
    verityDev = std::move(*verityDevRes);
    apex_data.device_name = device_name;
    blockDevice = verityDev.GetDevPath();

    Result<void> readAheadStatus =
        loop::configureReadAhead(verityDev.GetDevPath());
    if (!readAheadStatus) {
      return readAheadStatus.error();
    }
  }

  // TODO(b/122059364): Even though the kernel has created the verity
  // device, we still depend on ueventd to run to actually create the
  // device node in userspace. To solve this properly we should listen on
  // the netlink socket for uevents, or use inotify. For now, this will
  // have to do.
  Result<void> deviceStatus = waitForDevice(blockDevice, kWaitToBeCreated);
  if (!deviceStatus) {
    return deviceStatus.error();
  }

  // TODO: consider moving this inside RunVerifyFnInsideTempMount.
  if (mountOnVerity && verifyImage) {
    Result<void> verityStatus =
        readVerityDevice(blockDevice, (*verityData).desc->image_size);
    if (!verityStatus) {
      return verityStatus.error();
    }
  }

  uint32_t mountFlags = MS_NOATIME | MS_NODEV | MS_DIRSYNC | MS_RDONLY;
  if (apex.GetManifest().nocode()) {
    mountFlags |= MS_NOEXEC;
  }

  if (mount(blockDevice.c_str(), mountPoint.c_str(), "ext4", mountFlags,
            nullptr) == 0) {
    LOG(INFO) << "Successfully mounted package " << full_path << " on "
              << mountPoint;
    auto status = VerifyMountedImage(apex, mountPoint);
    if (!status) {
      umount2(mountPoint.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH);
      return Error() << "Failed to verify " << full_path << ": "
                     << status.error();
    }
    // Time to accept the temporaries as good.
    verityDev.Release();
    loopbackDevice.CloseGood();
    loop_for_hash.CloseGood();
    // TODO(b/120058143): Add loop_fo_hash to apex_data to clean up on unmount.

    scope_guard.Disable();  // Accept the mount.
    return apex_data;
  } else {
    return ErrnoError() << "Mounting failed for package " << full_path;
  }
}

Result<MountedApexData> VerifyAndTempMountPackage(
    const ApexFile& apex, const std::string& mount_point) {
  const std::string& package_id = GetPackageId(apex.GetManifest());
  LOG(DEBUG) << "Temp mounting " << package_id << " to " << mount_point;
  const std::string& temp_device_name = package_id + ".tmp";
  return MountPackageImpl(apex, mount_point, temp_device_name,
                          /* verifyImage = */ true);
}

Result<void> Unmount(const MountedApexData& data) {
  // Lazily try to umount whatever is mounted.
  if (umount2(data.mount_point.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH) != 0 &&
      errno != EINVAL && errno != ENOENT) {
    return ErrnoError() << "Failed to unmount directory " << data.mount_point;
  }
  // Attempt to delete the folder. If the folder is retained, other
  // data may be incorrect.
  if (rmdir(data.mount_point.c_str()) != 0) {
    PLOG(ERROR) << "Failed to rmdir directory " << data.mount_point;
  }

  // Try to free up the device-mapper device.
  if (!data.device_name.empty()) {
    const auto& status = DeleteVerityDevice(data.device_name);
    if (!status) {
      LOG(DEBUG) << "Failed to free device " << data.device_name << " : "
                 << status.error();
    }
  }

  // Try to free up the loop device.
  if (!data.loop_name.empty()) {
    auto log_fn = [](const std::string& path,
                     const std::string& id ATTRIBUTE_UNUSED) {
      LOG(VERBOSE) << "Freeing loop device " << path << "for unmount.";
    };
    loop::DestroyLoopDevice(data.loop_name, log_fn);
  }

  return {};
}

std::string GetPackageTempMountPoint(const ApexManifest& manifest) {
  return StringPrintf("%s.tmp",
                      apexd_private::GetPackageMountPoint(manifest).c_str());
}

template <typename VerifyFn>
Result<void> RunVerifyFnInsideTempMount(const ApexFile& apex,
                                        const VerifyFn& verify_fn) {
  // Temp mount image of this apex to validate it was properly signed;
  // this will also read the entire block device through dm-verity, so
  // we can be sure there is no corruption.
  const std::string& temp_mount_point =
      GetPackageTempMountPoint(apex.GetManifest());

  Result<MountedApexData> mount_status =
      VerifyAndTempMountPackage(apex, temp_mount_point);
  if (!mount_status) {
    LOG(ERROR) << "Failed to temp mount to " << temp_mount_point << " : "
               << mount_status.error();
    return mount_status.error();
  }
  auto cleaner = [&]() {
    LOG(DEBUG) << "Unmounting " << temp_mount_point;
    Result<void> status = Unmount(*mount_status);
    if (!status) {
      LOG(WARNING) << "Failed to unmount " << temp_mount_point << " : "
                   << status.error();
    }
  };
  auto scope_guard = android::base::make_scope_guard(cleaner);
  return verify_fn(temp_mount_point);
}

template <typename HookFn, typename HookCall>
Result<void> PrePostinstallPackages(const std::vector<ApexFile>& apexes,
                                    HookFn fn, HookCall call) {
  if (apexes.empty()) {
    return Errorf("Empty set of inputs");
  }

  // 1) Check whether the APEXes have hooks.
  bool has_hooks = false;
  for (const ApexFile& apex_file : apexes) {
    if (!(apex_file.GetManifest().*fn)().empty()) {
      has_hooks = true;
      break;
    }
  }

  // 2) If we found hooks, run the pre/post-install.
  if (has_hooks) {
    Result<void> install_status = (*call)(apexes);
    if (!install_status) {
      return install_status;
    }
  }

  return {};
}

Result<void> PreinstallPackages(const std::vector<ApexFile>& apexes) {
  return PrePostinstallPackages(apexes, &ApexManifest::preinstallhook,
                                &StagePreInstall);
}

Result<void> PostinstallPackages(const std::vector<ApexFile>& apexes) {
  return PrePostinstallPackages(apexes, &ApexManifest::postinstallhook,
                                &StagePostInstall);
}

template <typename RetType, typename Fn>
RetType HandlePackages(const std::vector<std::string>& paths, Fn fn) {
  // 1) Open all APEXes.
  std::vector<ApexFile> apex_files;
  for (const std::string& path : paths) {
    Result<ApexFile> apex_file = ApexFile::Open(path);
    if (!apex_file) {
      return apex_file.error();
    }
    apex_files.emplace_back(std::move(*apex_file));
  }

  // 2) Dispatch.
  return fn(apex_files);
}

Result<void> ValidateStagingShimApex(const ApexFile& to) {
  using android::base::StringPrintf;
  auto system_shim = ApexFile::Open(
      StringPrintf("%s/%s", kApexPackageSystemDir, shim::kSystemShimApexName));
  if (!system_shim) {
    return system_shim.error();
  }
  auto verify_fn = [&](const std::string& system_apex_path) {
    return shim::ValidateUpdate(system_apex_path, to.GetPath());
  };
  return RunVerifyFnInsideTempMount(*system_shim, verify_fn);
}

// A version of apex verification that happens during boot.
// This function should only verification checks that are necessary to run on
// each boot. Try to avoid putting expensive checks inside this function.
Result<void> VerifyPackageBoot(const ApexFile& apex_file) {
  Result<ApexVerityData> verity_or = apex_file.VerifyApexVerity();
  if (!verity_or) {
    return verity_or.error();
  }

  if (shim::IsShimApex(apex_file)) {
    // Validating shim is not a very cheap operation, but it's fine to perform
    // it here since it only runs during CTS tests and will never be triggered
    // during normal flow.
    const auto& status = ValidateStagingShimApex(apex_file);
    if (!status) {
      return status;
    }
  }
  return {};
}

// A version of apex verification that happens on submitStagedSession.
// This function contains checks that might be expensive to perform, e.g. temp
// mounting a package and reading entire dm-verity device, and shouldn't be run
// during boot.
Result<void> VerifyPackageInstall(const ApexFile& apex_file) {
  const auto& verify_package_boot_status = VerifyPackageBoot(apex_file);
  if (!verify_package_boot_status) {
    return verify_package_boot_status;
  }
  if (!kUpdatable) {
    return Error() << "Attempted to upgrade apex package "
                   << apex_file.GetPath()
                   << " on a device that doesn't support it";
  }
  Result<ApexVerityData> verity_or = apex_file.VerifyApexVerity();

  constexpr const auto kSuccessFn = [](const std::string& /*mount_point*/) {
    return Result<void>{};
  };
  return RunVerifyFnInsideTempMount(apex_file, kSuccessFn);
}

template <typename VerifyApexFn>
Result<std::vector<ApexFile>> verifyPackages(
    const std::vector<std::string>& paths, const VerifyApexFn& verify_apex_fn) {
  if (paths.empty()) {
    return Errorf("Empty set of inputs");
  }
  LOG(DEBUG) << "verifyPackages() for " << Join(paths, ',');

  auto verify_fn = [&](std::vector<ApexFile>& apexes) {
    for (const ApexFile& apex_file : apexes) {
      Result<void> status = verify_apex_fn(apex_file);
      if (!status) {
        return Result<std::vector<ApexFile>>(status.error());
      }
    }
    return Result<std::vector<ApexFile>>(std::move(apexes));
  };
  return HandlePackages<Result<std::vector<ApexFile>>>(paths, verify_fn);
}

Result<ApexFile> verifySessionDir(const int session_id) {
  std::string sessionDirPath = std::string(kStagedSessionsDir) + "/session_" +
                               std::to_string(session_id);
  LOG(INFO) << "Scanning " << sessionDirPath
            << " looking for packages to be validated";
  Result<std::vector<std::string>> scan = FindApexFilesByName(sessionDirPath);
  if (!scan) {
    LOG(WARNING) << scan.error();
    return scan.error();
  }

  if (scan->size() > 1) {
    return Errorf(
        "More than one APEX package found in the same session directory.");
  }

  auto verified = verifyPackages(*scan, VerifyPackageInstall);
  if (!verified) {
    return verified.error();
  }
  return std::move((*verified)[0]);
}

Result<void> ClearSessions() {
  auto sessions = ApexSession::GetSessions();
  int cnt = 0;
  for (ApexSession& session : sessions) {
    Result<void> status = session.DeleteSession();
    if (!status) {
      return status;
    }
    cnt++;
  }
  if (cnt > 0) {
    LOG(DEBUG) << "Deleted " << cnt << " sessions";
  }
  return {};
}

Result<void> DeleteBackup() {
  auto exists = PathExists(std::string(kApexBackupDir));
  if (!exists) {
    return Error() << "Can't clean " << kApexBackupDir << " : "
                   << exists.error();
  }
  if (!*exists) {
    LOG(DEBUG) << kApexBackupDir << " does not exist. Nothing to clean";
    return {};
  }
  return DeleteDirContent(std::string(kApexBackupDir));
}

Result<void> BackupActivePackages() {
  LOG(DEBUG) << "Initializing  backup of " << kActiveApexPackagesDataDir;

  // Previous restore might've delete backups folder.
  auto create_status = createDirIfNeeded(kApexBackupDir, 0700);
  if (!create_status) {
    return Error() << "Backup failed : " << create_status.error();
  }

  auto apex_active_exists = PathExists(std::string(kActiveApexPackagesDataDir));
  if (!apex_active_exists) {
    return Error() << "Backup failed : " << apex_active_exists.error();
  }
  if (!*apex_active_exists) {
    LOG(DEBUG) << kActiveApexPackagesDataDir
               << " does not exist. Nothing to backup";
    return {};
  }

  auto active_packages = FindApexFilesByName(kActiveApexPackagesDataDir);
  if (!active_packages) {
    return Error() << "Backup failed : " << active_packages.error();
  }

  auto cleanup_status = DeleteBackup();
  if (!cleanup_status) {
    return Error() << "Backup failed : " << cleanup_status.error();
  }

  auto backup_path_fn = [](const ApexFile& apex_file) {
    return StringPrintf("%s/%s%s", kApexBackupDir,
                        GetPackageId(apex_file.GetManifest()).c_str(),
                        kApexPackageSuffix);
  };

  auto deleter = []() {
    auto status = DeleteDirContent(std::string(kApexBackupDir));
    if (!status) {
      LOG(ERROR) << "Failed to cleanup " << kApexBackupDir << " : "
                 << status.error();
    }
  };
  auto scope_guard = android::base::make_scope_guard(deleter);

  for (const std::string& path : *active_packages) {
    Result<ApexFile> apex_file = ApexFile::Open(path);
    if (!apex_file) {
      return Error() << "Backup failed : " << apex_file.error();
    }
    const auto& dest_path = backup_path_fn(*apex_file);
    if (link(apex_file->GetPath().c_str(), dest_path.c_str()) != 0) {
      return ErrnoError() << "Failed to backup " << apex_file->GetPath();
    }
  }

  scope_guard.Disable();  // Accept the backup.
  return {};
}

Result<void> DoRollback(ApexSession& session) {
  if (gInFsCheckpointMode) {
    // We will roll back automatically when we reboot
    return {};
  }
  auto scope_guard = android::base::make_scope_guard([&]() {
    auto st = session.UpdateStateAndCommit(SessionState::ROLLBACK_FAILED);
    LOG(DEBUG) << "Marking " << session << " as failed to rollback";
    if (!st) {
      LOG(WARNING) << "Failed to mark session " << session
                   << " as failed to rollback : " << st.error();
    }
  });

  auto backup_exists = PathExists(std::string(kApexBackupDir));
  if (!backup_exists) {
    return backup_exists.error();
  }
  if (!*backup_exists) {
    return Error() << kApexBackupDir << " does not exist";
  }

  struct stat stat_data;
  if (stat(kActiveApexPackagesDataDir, &stat_data) != 0) {
    return ErrnoError() << "Failed to access " << kActiveApexPackagesDataDir;
  }

  LOG(DEBUG) << "Deleting existing packages in " << kActiveApexPackagesDataDir;
  auto delete_status =
      DeleteDirContent(std::string(kActiveApexPackagesDataDir));
  if (!delete_status) {
    return delete_status;
  }

  LOG(DEBUG) << "Renaming " << kApexBackupDir << " to "
             << kActiveApexPackagesDataDir;
  if (rename(kApexBackupDir, kActiveApexPackagesDataDir) != 0) {
    return ErrnoError() << "Failed to rename " << kApexBackupDir << " to "
                        << kActiveApexPackagesDataDir;
  }

  LOG(DEBUG) << "Restoring original permissions for "
             << kActiveApexPackagesDataDir;
  if (chmod(kActiveApexPackagesDataDir, stat_data.st_mode & ALLPERMS) != 0) {
    // TODO: should we wipe out /data/apex/active if chmod fails?
    return ErrnoError() << "Failed to restore original permissions for "
                        << kActiveApexPackagesDataDir;
  }

  scope_guard.Disable();  // Rollback succeeded. Accept state.
  return {};
}

Result<void> RollbackStagedSession(ApexSession& session) {
  // If the session is staged, it hasn't been activated yet, and we just need
  // to update its state to prevent it from being activated later.
  return session.UpdateStateAndCommit(SessionState::ROLLED_BACK);
}

Result<void> RollbackActivatedSession(ApexSession& session) {
  if (gInFsCheckpointMode) {
    LOG(DEBUG) << "Checkpoint mode is enabled";
    // On checkpointing devices, our modifications on /data will be
    // automatically rolled back when we abort changes. Updating the session
    // state is pointless here, as it will be rolled back as well.
    return {};
  }

  auto status =
      session.UpdateStateAndCommit(SessionState::ROLLBACK_IN_PROGRESS);
  if (!status) {
    // TODO: should we continue with a rollback?
    return Error() << "Rollback of session " << session
                   << " failed : " << status.error();
  }

  status = DoRollback(session);
  if (!status) {
    return Error() << "Rollback of session " << session
                   << " failed : " << status.error();
  }

  status = session.UpdateStateAndCommit(SessionState::ROLLED_BACK);
  if (!status) {
    LOG(WARNING) << "Failed to mark session " << session
                 << " as rolled back : " << status.error();
  }

  return {};
}

Result<void> RollbackSession(ApexSession& session) {
  LOG(DEBUG) << "Initializing rollback of " << session;

  switch (session.GetState()) {
    case SessionState::ROLLBACK_IN_PROGRESS:
      [[clang::fallthrough]];
    case SessionState::ROLLED_BACK:
      return {};
    case SessionState::STAGED:
      return RollbackStagedSession(session);
    case SessionState::ACTIVATED:
      return RollbackActivatedSession(session);
    default:
      return Error() << "Can't restore session " << session
                     << " : session is in a wrong state";
  }
}

Result<void> ResumeRollback(ApexSession& session) {
  auto backup_exists = PathExists(std::string(kApexBackupDir));
  if (!backup_exists) {
    return backup_exists.error();
  }
  if (*backup_exists) {
    auto rollback_status = DoRollback(session);
    if (!rollback_status) {
      return rollback_status;
    }
  }
  auto status = session.UpdateStateAndCommit(SessionState::ROLLED_BACK);
  if (!status) {
    LOG(WARNING) << "Failed to mark session " << session
                 << " as rolled back : " << status.error();
  }
  return {};
}

Result<void> UnmountPackage(const ApexFile& apex, bool allow_latest) {
  LOG(VERBOSE) << "Unmounting " << GetPackageId(apex.GetManifest());

  const ApexManifest& manifest = apex.GetManifest();

  std::optional<MountedApexData> data;
  bool latest = false;

  auto fn = [&](const MountedApexData& d, bool l) {
    if (d.full_path == apex.GetPath()) {
      data.emplace(d);
      latest = l;
    }
  };
  gMountedApexes.ForallMountedApexes(manifest.name(), fn);

  if (!data) {
    return Error() << "Did not find " << apex.GetPath();
  }

  if (latest) {
    if (!allow_latest) {
      return Error() << "Package " << apex.GetPath() << " is active";
    }
    std::string mount_point = apexd_private::GetActiveMountPoint(manifest);
    LOG(VERBOSE) << "Unmounting and deleting " << mount_point;
    if (umount2(mount_point.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH) != 0) {
      return ErrnoError() << "Failed to unmount " << mount_point;
    }
    if (rmdir(mount_point.c_str()) != 0) {
      PLOG(ERROR) << "Could not rmdir " << mount_point;
      // Continue here.
    }
  }

  // Clean up gMountedApexes now, even though we're not fully done.
  gMountedApexes.RemoveMountedApex(manifest.name(), apex.GetPath());
  return Unmount(*data);
}

}  // namespace

namespace apexd_private {

Result<void> MountPackage(const ApexFile& apex, const std::string& mountPoint) {
  auto ret =
      MountPackageImpl(apex, mountPoint, GetPackageId(apex.GetManifest()),
                       /* verifyImage = */ false);
  if (!ret) {
    return ret.error();
  }

  gMountedApexes.AddMountedApex(apex.GetManifest().name(), false,
                                std::move(*ret));
  return {};
}

Result<void> UnmountPackage(const ApexFile& apex) {
  return android::apex::UnmountPackage(apex, /* allow_latest= */ false);
}

bool IsMounted(const std::string& name, const std::string& full_path) {
  bool found_mounted = false;
  gMountedApexes.ForallMountedApexes(
      name, [&](const MountedApexData& data, bool latest ATTRIBUTE_UNUSED) {
        if (full_path == data.full_path) {
          found_mounted = true;
        }
      });
  return found_mounted;
}

std::string GetPackageMountPoint(const ApexManifest& manifest) {
  return StringPrintf("%s/%s", kApexRoot, GetPackageId(manifest).c_str());
}

std::string GetActiveMountPoint(const ApexManifest& manifest) {
  return StringPrintf("%s/%s", kApexRoot, manifest.name().c_str());
}

}  // namespace apexd_private

Result<void> resumeRollbackIfNeeded() {
  auto session = ApexSession::GetActiveSession();
  if (!session) {
    return session.error();
  }
  if (!session->has_value()) {
    return {};
  }
  if ((**session).GetState() == SessionState::ROLLBACK_IN_PROGRESS) {
    // This means that phone was rebooted during the rollback. Resuming it.
    return ResumeRollback(**session);
  }
  return {};
}

Result<void> activatePackageImpl(const ApexFile& apex_file) {
  const ApexManifest& manifest = apex_file.GetManifest();

  if (gBootstrap && !isBootstrapApex(apex_file)) {
    LOG(INFO) << "Skipped when bootstrapping";
    return {};
  } else if (!kUpdatable && !gBootstrap && isBootstrapApex(apex_file)) {
    LOG(INFO) << "Package already activated in bootstrap";
    return {};
  }

  // See whether we think it's active, and do not allow to activate the same
  // version. Also detect whether this is the highest version.
  // We roll this into a single check.
  bool is_newest_version = true;
  bool found_other_version = false;
  bool version_found_mounted = false;
  {
    uint64_t new_version = manifest.version();
    bool version_found_active = false;
    gMountedApexes.ForallMountedApexes(
        manifest.name(), [&](const MountedApexData& data, bool latest) {
          Result<ApexFile> otherApex = ApexFile::Open(data.full_path);
          if (!otherApex) {
            return;
          }
          found_other_version = true;
          if (static_cast<uint64_t>(otherApex->GetManifest().version()) ==
              new_version) {
            version_found_mounted = true;
            version_found_active = latest;
          }
          if (static_cast<uint64_t>(otherApex->GetManifest().version()) >
              new_version) {
            is_newest_version = false;
          }
        });
    if (version_found_active) {
      LOG(DEBUG) << "Package " << manifest.name() << " with version "
                 << manifest.version() << " already active";
      return {};
    }
  }

  const std::string& mountPoint = apexd_private::GetPackageMountPoint(manifest);

  if (!version_found_mounted) {
    Result<void> mountStatus =
        apexd_private::MountPackage(apex_file, mountPoint);
    if (!mountStatus) {
      return mountStatus;
    }
  }

  bool mounted_latest = false;
  if (is_newest_version) {
    const Result<void>& update_st = apexd_private::BindMount(
        apexd_private::GetActiveMountPoint(manifest), mountPoint);
    mounted_latest = update_st.has_value();
    if (!update_st) {
      return Error() << "Failed to update package " << manifest.name()
                     << " to version " << manifest.version() << " : "
                     << update_st.error();
    }
  }
  if (mounted_latest) {
    gMountedApexes.SetLatest(manifest.name(), apex_file.GetPath());
  }

  LOG(DEBUG) << "Successfully activated " << apex_file.GetPath()
             << " package_name: " << manifest.name()
             << " version: " << manifest.version();
  return {};
}

Result<void> activatePackage(const std::string& full_path) {
  LOG(INFO) << "Trying to activate " << full_path;

  Result<ApexFile> apex_file = ApexFile::Open(full_path);
  if (!apex_file) {
    return apex_file.error();
  }
  return activatePackageImpl(*apex_file);
}

Result<void> deactivatePackage(const std::string& full_path) {
  LOG(INFO) << "Trying to deactivate " << full_path;

  Result<ApexFile> apexFile = ApexFile::Open(full_path);
  if (!apexFile) {
    return apexFile.error();
  }

  return UnmountPackage(*apexFile, /* allow_latest= */ true);
}

std::vector<ApexFile> getActivePackages() {
  std::vector<ApexFile> ret;
  gMountedApexes.ForallMountedApexes(
      [&](const std::string&, const MountedApexData& data, bool latest) {
        if (!latest) {
          return;
        }

        Result<ApexFile> apexFile = ApexFile::Open(data.full_path);
        if (!apexFile) {
          // TODO: Fail?
          return;
        }
        ret.emplace_back(std::move(*apexFile));
      });

  return ret;
}

namespace {
std::unordered_map<std::string, uint64_t> GetActivePackagesMap() {
  std::vector<ApexFile> active_packages = getActivePackages();
  std::unordered_map<std::string, uint64_t> ret;
  for (const auto& package : active_packages) {
    const ApexManifest& manifest = package.GetManifest();
    ret.insert({manifest.name(), manifest.version()});
  }
  return ret;
}

}  // namespace

std::vector<ApexFile> getFactoryPackages() {
  std::vector<ApexFile> ret;
  for (const auto& dir : kApexPackageBuiltinDirs) {
    auto apex_files = FindApexFilesByName(dir);
    if (!apex_files) {
      LOG(ERROR) << apex_files.error();
      continue;
    }
    for (const std::string& path : *apex_files) {
      Result<ApexFile> apex_file = ApexFile::Open(path);
      if (!apex_file) {
        LOG(ERROR) << apex_file.error();
      } else {
        ret.emplace_back(std::move(*apex_file));
      }
    }
  }
  return ret;
}

Result<ApexFile> getActivePackage(const std::string& packageName) {
  std::vector<ApexFile> packages = getActivePackages();
  for (ApexFile& apex : packages) {
    if (apex.GetManifest().name() == packageName) {
      return std::move(apex);
    }
  }

  return ErrnoError() << "Cannot find matching package for: " << packageName;
}

Result<void> abortActiveSession() {
  auto session_or_none = ApexSession::GetActiveSession();
  if (!session_or_none) {
    return session_or_none.error();
  }
  if (session_or_none->has_value()) {
    auto& session = session_or_none->value();
    LOG(DEBUG) << "Aborting active session " << session;
    switch (session.GetState()) {
      case SessionState::VERIFIED:
        [[clang::fallthrough]];
      case SessionState::STAGED:
        return session.DeleteSession();
      case SessionState::ACTIVATED:
        return RollbackActivatedSession(session);
      default:
        return Error() << "Session " << session << " can't be aborted";
    }
  } else {
    LOG(DEBUG) << "There are no active sessions";
    return {};
  }
}

Result<void> scanPackagesDirAndActivate(const char* apex_package_dir) {
  LOG(INFO) << "Scanning " << apex_package_dir << " looking for APEX packages.";

  Result<std::vector<std::string>> scan = FindApexFilesByName(apex_package_dir);
  if (!scan) {
    return Error() << "Failed to scan " << apex_package_dir << " : "
                   << scan.error();
  }

  const auto& packages_with_code = GetActivePackagesMap();

  std::vector<std::string> failed_pkgs;
  size_t activated_cnt = 0;
  size_t skipped_cnt = 0;
  for (const std::string& name : *scan) {
    LOG(INFO) << "Found " << name;

    Result<ApexFile> apex_file = ApexFile::Open(name);
    if (!apex_file) {
      LOG(ERROR) << "Failed to activate " << name << " : " << apex_file.error();
      failed_pkgs.push_back(name);
      continue;
    }

    uint64_t new_version =
        static_cast<uint64_t>(apex_file->GetManifest().version());
    const auto& it = packages_with_code.find(apex_file->GetManifest().name());
    if (it != packages_with_code.end() && it->second >= new_version) {
      LOG(INFO) << "Skipping activation of " << name
                << " same package with higher version " << it->second
                << " is already active";
      skipped_cnt++;
      continue;
    }

    Result<void> res = activatePackageImpl(*apex_file);
    if (!res) {
      LOG(ERROR) << "Failed to activate " << name << " : " << res.error();
      failed_pkgs.push_back(name);
    } else {
      activated_cnt++;
    }
  }

  if (!failed_pkgs.empty()) {
    return Error() << "Failed to activate following packages : "
                   << Join(failed_pkgs, ',');
  }

  LOG(INFO) << "Activated " << activated_cnt
            << " packages. Skipped: " << skipped_cnt;
  return {};
}

void scanStagedSessionsDirAndStage() {
  LOG(INFO) << "Scanning " << kApexSessionsDir
            << " looking for sessions to be activated.";

  auto stagedSessions = ApexSession::GetSessionsInState(SessionState::STAGED);
  for (auto& session : stagedSessions) {
    auto sessionId = session.GetId();

    auto session_failed_fn = [&]() {
      LOG(WARNING) << "Marking session " << sessionId << " as failed.";
      auto st = session.UpdateStateAndCommit(SessionState::ACTIVATION_FAILED);
      if (!st) {
        LOG(WARNING) << "Failed to mark session " << sessionId
                     << " as failed : " << st.error();
      }
    };
    auto scope_guard = android::base::make_scope_guard(session_failed_fn);

    std::vector<std::string> dirsToScan;
    if (session.GetChildSessionIds().empty()) {
      dirsToScan.push_back(std::string(kStagedSessionsDir) + "/session_" +
                           std::to_string(sessionId));
    } else {
      for (auto childSessionId : session.GetChildSessionIds()) {
        dirsToScan.push_back(std::string(kStagedSessionsDir) + "/session_" +
                             std::to_string(childSessionId));
      }
    }

    std::vector<std::string> apexes;
    bool scanSuccessful = true;
    for (const auto& dirToScan : dirsToScan) {
      Result<std::vector<std::string>> scan = FindApexFilesByName(dirToScan);
      if (!scan) {
        LOG(WARNING) << scan.error();
        scanSuccessful = false;
        break;
      }

      if (scan->size() > 1) {
        LOG(WARNING) << "More than one APEX package found in the same session "
                     << "directory " << dirToScan << ", skipping activation.";
        scanSuccessful = false;
        break;
      }

      if (scan->empty()) {
        LOG(WARNING) << "No APEX packages found while scanning " << dirToScan
                     << " session id: " << sessionId << ".";
        scanSuccessful = false;
        break;
      }
      apexes.push_back(std::move((*scan)[0]));
    }

    if (!scanSuccessful) {
      continue;
    }

    // Run postinstall, if necessary.
    Result<void> postinstall_status = postinstallPackages(apexes);
    if (!postinstall_status) {
      LOG(ERROR) << "Postinstall failed for session "
                 << std::to_string(sessionId) << ": "
                 << postinstall_status.error();
      continue;
    }

    const Result<void> result = stagePackages(apexes);
    if (!result) {
      LOG(ERROR) << "Activation failed for packages " << Join(apexes, ',')
                 << ": " << result.error();
      continue;
    }

    // Session was OK, release scopeguard.
    scope_guard.Disable();

    auto st = session.UpdateStateAndCommit(SessionState::ACTIVATED);
    if (!st) {
      LOG(ERROR) << "Failed to mark " << session
                 << " as activated : " << st.error();
    }
  }
}

Result<void> preinstallPackages(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return Errorf("Empty set of inputs");
  }
  LOG(DEBUG) << "preinstallPackages() for " << Join(paths, ',');
  return HandlePackages<Result<void>>(paths, PreinstallPackages);
}

Result<void> postinstallPackages(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return Errorf("Empty set of inputs");
  }
  LOG(DEBUG) << "postinstallPackages() for " << Join(paths, ',');
  return HandlePackages<Result<void>>(paths, PostinstallPackages);
}

namespace {
std::string StageDestPath(const ApexFile& apex_file) {
  return StringPrintf("%s/%s%s", kActiveApexPackagesDataDir,
                      GetPackageId(apex_file.GetManifest()).c_str(),
                      kApexPackageSuffix);
}

}  // namespace

Result<void> stagePackages(const std::vector<std::string>& tmpPaths) {
  if (tmpPaths.empty()) {
    return Errorf("Empty set of inputs");
  }
  LOG(DEBUG) << "stagePackages() for " << Join(tmpPaths, ',');

  // Note: this function is temporary. As such the code is not optimized, e.g.,
  //       it will open ApexFiles multiple times.

  // 1) Verify all packages.
  auto verify_status = verifyPackages(tmpPaths, VerifyPackageBoot);
  if (!verify_status) {
    return verify_status.error();
  }

  // Make sure that kActiveApexPackagesDataDir exists.
  auto create_dir_status =
      createDirIfNeeded(std::string(kActiveApexPackagesDataDir), 0750);
  if (!create_dir_status) {
    return create_dir_status.error();
  }

  // 2) Now stage all of them.

  // Ensure the APEX gets removed on failure.
  std::unordered_set<std::string> staged_files;
  auto deleter = [&staged_files]() {
    for (const std::string& staged_path : staged_files) {
      if (TEMP_FAILURE_RETRY(unlink(staged_path.c_str())) != 0) {
        PLOG(ERROR) << "Unable to unlink " << staged_path;
      }
    }
  };
  auto scope_guard = android::base::make_scope_guard(deleter);

  std::unordered_set<std::string> staged_packages;
  for (const std::string& path : tmpPaths) {
    Result<ApexFile> apex_file = ApexFile::Open(path);
    if (!apex_file) {
      return apex_file.error();
    }
    std::string dest_path = StageDestPath(*apex_file);
    if (access(dest_path.c_str(), F_OK) == 0) {
      LOG(DEBUG) << dest_path << " already exists. Skipping";
      continue;
    }

    if (link(apex_file->GetPath().c_str(), dest_path.c_str()) != 0) {
      // TODO: Get correct binder error status.
      return ErrnoError() << "Unable to link " << apex_file->GetPath() << " to "
                          << dest_path;
    }
    staged_files.insert(dest_path);
    staged_packages.insert(apex_file->GetManifest().name());

    LOG(DEBUG) << "Success linking " << apex_file->GetPath() << " to "
               << dest_path;
  }

  scope_guard.Disable();  // Accept the state.

  return RemovePreviouslyActiveApexFiles(staged_packages, staged_files);
}

Result<void> unstagePackages(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return Errorf("Empty set of inputs");
  }
  LOG(DEBUG) << "unstagePackages() for " << Join(paths, ',');

  // TODO: to make unstage safer, we can copy to be unstaged packages to a
  // temporary folder and restore state from it in case unstagePackages fails.

  for (const std::string& path : paths) {
    if (access(path.c_str(), F_OK) != 0) {
      return ErrnoError() << "Can't access " << path;
    }
  }

  for (const std::string& path : paths) {
    if (unlink(path.c_str()) != 0) {
      return ErrnoError() << "Can't unlink " << path;
    }
  }

  return {};
}

Result<void> rollbackStagedSessionIfAny() {
  auto session = ApexSession::GetActiveSession();
  if (!session) {
    return session.error();
  }
  if (!session->has_value()) {
    LOG(WARNING) << "No session to rollback";
    return {};
  }
  if ((*session)->GetState() == SessionState::STAGED) {
    LOG(INFO) << "Rolling back session " << **session;
    return RollbackStagedSession(**session);
  }
  return Error() << "Can't rollback " << **session
                 << " because it is not in STAGED state";
}

Result<void> rollbackActiveSession() {
  auto session = ApexSession::GetActiveSession();
  if (!session) {
    return Error() << "Failed to get active session : " << session.error();
  } else if (!session->has_value()) {
    return Error() << "Rollback requested, when there are no active sessions.";
  } else {
    return RollbackSession(*(*session));
  }
}

Result<void> rollbackActiveSessionAndReboot() {
  auto status = rollbackActiveSession();
  if (!status) {
    return status;
  }
  LOG(ERROR) << "Successfully rolled back. Time to reboot device.";
  if (gInFsCheckpointMode) {
    Result<void> res = gVoldService->AbortChanges(
        "apexd_initiated" /* message */, false /* retry */);
    if (!res) {
      LOG(ERROR) << res.error();
    }
  }
  Reboot();
  return {};
}

int onBootstrap() {
  gBootstrap = true;

  Result<void> preAllocate = preAllocateLoopDevices();
  if (!preAllocate) {
    LOG(ERROR) << "Failed to pre-allocate loop devices : "
               << preAllocate.error();
  }

  Result<void> status = collectApexKeys({kApexPackageSystemDir});
  if (!status) {
    LOG(ERROR) << "Failed to collect APEX keys : " << status.error();
    return 1;
  }

  // Activate built-in APEXes for processes launched before /data is mounted.
  status = scanPackagesDirAndActivate(kApexPackageSystemDir);
  if (!status) {
    LOG(ERROR) << "Failed to activate APEX files in " << kApexPackageSystemDir
               << " : " << status.error();
    return 1;
  }
  LOG(INFO) << "Bootstrapping done";
  return 0;
}

Result<void> remountApexFile(const std::string& path) {
  auto ret = deactivatePackage(path);
  if (!ret) return ret.error();

  ret = activatePackage(path);
  if (!ret) return ret.error();

  return {};
}

Result<void> monitorBuiltinDirs() {
  int fd = inotify_init1(IN_CLOEXEC);
  if (fd == -1) {
    return ErrnoErrorf("inotify_init failed");
  }
  std::map<int, std::string> desc_to_dir;
  for (const auto& dir : kApexPackageBuiltinDirs) {
    int desc = inotify_add_watch(fd, dir.c_str(), IN_CREATE | IN_MODIFY);
    if (desc == -1 && errno != ENOENT) {
      // don't complain about missing directories like /product/apex
      return ErrnoErrorf("failed to add watch on {}", dir);
    }
    desc_to_dir.emplace(desc, dir);
  }
  static std::thread th([fd, desc_to_dir]() -> void {
    constexpr int num_events = 100;
    constexpr size_t average_path_length = 50;
    char buffer[num_events *
                (sizeof(struct inotify_event) + average_path_length)];
    while (true) {
      ssize_t length = read(fd, buffer, sizeof(buffer));
      if (length < 0) {
        PLOG(ERROR) << "failed to read inotify event: " << strerror(errno);
        continue;
      }
      int i = 0;
      while (i < length) {
        struct inotify_event* e = (struct inotify_event*)&buffer[i];
        if (e->len > 0 && (e->mask & (IN_CREATE | IN_MODIFY)) != 0) {
          if (desc_to_dir.find(e->wd) == desc_to_dir.end()) {
            LOG(ERROR) << "unexpected watch descriptor " << e->wd
                       << " for name: " << e->name;
          } else {
            std::string path = desc_to_dir.at(e->wd) + "/" + e->name;
            auto ret = remountApexFile(path);
            if (!ret) {
              LOG(ERROR) << ret.error().message();
            } else {
              LOG(INFO) << path << " remounted because it was changed";
            }
          }
        }
        i += sizeof(struct inotify_event) + e->len;
      }
    }
  });

  return {};
}

void onStart(CheckpointInterface* checkpoint_service) {
  LOG(INFO) << "Marking APEXd as starting";
  if (!android::base::SetProperty(kApexStatusSysprop, kApexStatusStarting)) {
    PLOG(ERROR) << "Failed to set " << kApexStatusSysprop << " to "
                << kApexStatusStarting;
  }

  if (checkpoint_service != nullptr) {
    gVoldService = checkpoint_service;
    Result<bool> supports_fs_checkpoints =
        gVoldService->SupportsFsCheckpoints();
    if (supports_fs_checkpoints) {
      gSupportsFsCheckpoints = *supports_fs_checkpoints;
    } else {
      LOG(ERROR) << "Failed to check if filesystem checkpoints are supported: "
                 << supports_fs_checkpoints.error();
    }
    if (gSupportsFsCheckpoints) {
      Result<bool> needs_checkpoint = gVoldService->NeedsCheckpoint();
      if (needs_checkpoint) {
        gInFsCheckpointMode = *needs_checkpoint;
      } else {
        LOG(ERROR) << "Failed to check if we're in filesystem checkpoint mode: "
                   << needs_checkpoint.error();
      }
    }
  }

  // Ask whether we should roll back any staged sessions; this can happen if
  // we've exceeded the retry count on a device that supports filesystem
  // checkpointing.
  if (gSupportsFsCheckpoints) {
    Result<bool> needs_rollback = gVoldService->NeedsRollback();
    if (!needs_rollback) {
      LOG(ERROR) << "Failed to check if we need a rollback: "
                 << needs_rollback.error();
    } else if (*needs_rollback) {
      LOG(INFO) << "Exceeded number of session retries ("
                << kNumRetriesWhenCheckpointingEnabled
                << "). Starting a rollback";
      Result<void> status = rollbackStagedSessionIfAny();
      if (!status) {
        LOG(ERROR)
            << "Failed to roll back (as requested by fs checkpointing) : "
            << status.error();
      }
    }
  }

  Result<void> status = collectApexKeys(kApexPackageBuiltinDirs);
  if (!status) {
    LOG(ERROR) << "Failed to collect APEX keys : " << status.error();
    return;
  }

  gMountedApexes.PopulateFromMounts();

  // Activate APEXes from /data/apex. If one in the directory is newer than the
  // system one, the new one will eclipse the old one.
  scanStagedSessionsDirAndStage();
  status = resumeRollbackIfNeeded();
  if (!status) {
    LOG(ERROR) << "Failed to resume rollback : " << status.error();
  }

  status = scanPackagesDirAndActivate(kActiveApexPackagesDataDir);
  if (!status) {
    LOG(ERROR) << "Failed to activate packages from "
               << kActiveApexPackagesDataDir << " : " << status.error();
    Result<void> rollback_status = rollbackActiveSessionAndReboot();
    if (!rollback_status) {
      // TODO: should we kill apexd in this case?
      LOG(ERROR) << "Failed to rollback : " << rollback_status.error();
    }
  }

  for (const auto& dir : kApexPackageBuiltinDirs) {
    // TODO(b/123622800): if activation failed, rollback and reboot.
    status = scanPackagesDirAndActivate(dir.c_str());
    if (!status) {
      // This should never happen. Like **really** never.
      // TODO: should we kill apexd in this case?
      LOG(ERROR) << "Failed to activate packages from " << dir << " : "
                 << status.error();
    }
  }

  if (android::base::GetBoolProperty("ro.debuggable", false)) {
    status = monitorBuiltinDirs();
    if (!status) {
      LOG(ERROR) << "cannot monitor built-in dirs: " << status.error();
    }
  }
}

void onAllPackagesReady() {
  // Set a system property to let other components to know that APEXs are
  // correctly mounted and ready to be used. Before using any file from APEXs,
  // they can query this system property to ensure that they are okay to
  // access. Or they may have a on-property trigger to delay a task until
  // APEXs become ready.
  LOG(INFO) << "Marking APEXd as ready";
  if (!android::base::SetProperty(kApexStatusSysprop, kApexStatusReady)) {
    PLOG(ERROR) << "Failed to set " << kApexStatusSysprop << " to "
                << kApexStatusReady;
  }
}

Result<std::vector<ApexFile>> submitStagedSession(
    const int session_id, const std::vector<int>& child_session_ids) {
  bool needsBackup = true;
  Result<void> cleanup_status = ClearSessions();
  if (!cleanup_status) {
    return cleanup_status.error();
  }

  if (gSupportsFsCheckpoints) {
    Result<void> checkpoint_status =
        gVoldService->StartCheckpoint(kNumRetriesWhenCheckpointingEnabled);
    if (!checkpoint_status) {
      // The device supports checkpointing, but we could not start it;
      // log a warning, but do continue, since we can live without it.
      LOG(WARNING) << "Failed to start filesystem checkpoint on device that "
                      "should support it: "
                   << checkpoint_status.error();
    } else {
      needsBackup = false;
    }
  }

  if (needsBackup) {
    Result<void> backup_status = BackupActivePackages();
    if (!backup_status) {
      return backup_status.error();
    }
  }

  std::vector<int> ids_to_scan;
  if (!child_session_ids.empty()) {
    ids_to_scan = child_session_ids;
  } else {
    ids_to_scan = {session_id};
  }

  std::vector<ApexFile> ret;
  for (int id_to_scan : ids_to_scan) {
    auto verified = verifySessionDir(id_to_scan);
    if (!verified) {
      return verified.error();
    }
    ret.push_back(std::move(*verified));
  }

  // Run preinstall, if necessary.
  Result<void> preinstall_status = PreinstallPackages(ret);
  if (!preinstall_status) {
    return preinstall_status.error();
  }

  auto session = ApexSession::CreateSession(session_id);
  if (!session) {
    return session.error();
  }
  (*session).SetChildSessionIds(child_session_ids);
  Result<void> commit_status =
      (*session).UpdateStateAndCommit(SessionState::VERIFIED);
  if (!commit_status) {
    return commit_status.error();
  }

  return ret;
}

Result<void> markStagedSessionReady(const int session_id) {
  auto session = ApexSession::GetSession(session_id);
  if (!session) {
    return session.error();
  }
  // We should only accept sessions in SessionState::VERIFIED or
  // SessionState::STAGED state. In the SessionState::STAGED case, this
  // function is effectively a no-op.
  auto session_state = (*session).GetState();
  if (session_state == SessionState::STAGED) {
    return {};
  }
  if (session_state == SessionState::VERIFIED) {
    return (*session).UpdateStateAndCommit(SessionState::STAGED);
  }
  return Error() << "Invalid state for session " << session_id
                 << ". Cannot mark it as ready.";
}

Result<void> markStagedSessionSuccessful(const int session_id) {
  auto session = ApexSession::GetSession(session_id);
  if (!session) {
    return session.error();
  }
  // Only SessionState::ACTIVATED or SessionState::SUCCESS states are accepted.
  // In the SessionState::SUCCESS state, this function is a no-op.
  if (session->GetState() == SessionState::SUCCESS) {
    return {};
  } else if (session->GetState() == SessionState::ACTIVATED) {
    auto cleanup_status = DeleteBackup();
    if (!cleanup_status) {
      return Error() << "Failed to mark session " << *session
                     << " as successful : " << cleanup_status.error();
    }
    return session->UpdateStateAndCommit(SessionState::SUCCESS);
  } else {
    return Error() << "Session " << *session << " can not be marked successful";
  }
}

// Find dangling mounts and unmount them.
// If one is on /data/apex/active, remove it.
void unmountDanglingMounts() {
  std::multimap<std::string, MountedApexData> danglings;
  gMountedApexes.ForallMountedApexes([&](const std::string& package,
                                         const MountedApexData& data,
                                         bool latest) {
    if (!latest) {
      danglings.insert({package, data});
    }
  });

  for (const auto& [package, data] : danglings) {
    const std::string& path = data.full_path;
    LOG(VERBOSE) << "Unmounting " << data.mount_point;
    gMountedApexes.RemoveMountedApex(package, path);
    if (auto st = Unmount(data); !st) {
      LOG(ERROR) << st.error();
    }
    if (StartsWith(path, kActiveApexPackagesDataDir)) {
      LOG(VERBOSE) << "Deleting old APEX " << path;
      if (unlink(path.c_str()) != 0) {
        PLOG(ERROR) << "Failed to delete " << path;
      }
    }
  }

  RemoveObsoleteHashTrees();
}

}  // namespace apex
}  // namespace android
