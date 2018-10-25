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

#include "apex_file.h"
#include "apex_manifest.h"
#include "string_log.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <libavb/libavb.h>
#include <libdm/dm.h>
#include <libdm/dm_table.h>
#include <libdm/dm_target.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>

using android::base::Basename;
using android::base::EndsWith;
using android::base::ReadFullyAtOffset;
using android::base::StringPrintf;
using android::base::unique_fd;
using android::dm::DeviceMapper;
using android::dm::DmTable;
using android::dm::DmTargetVerity;

namespace android {
namespace apex {

namespace {

static constexpr const char* kApexPackageSuffix = ".apex";
static constexpr const char* kApexRoot = "/apex";
static constexpr const char* kApexLoopIdPrefix = "apex:";
static constexpr const char* kApexKeyDirectory = "/system/etc/security/apex/";
static constexpr const char* kApexKeyProp = "apex.key";

static constexpr int kVbMetaMaxSize = 64 * 1024;

Status createLoopDevice(const std::string& target, const int32_t imageOffset,
                        const size_t imageSize, std::string& out_device) {
  unique_fd ctl_fd(open("/dev/loop-control", O_RDWR | O_CLOEXEC));
  if (ctl_fd.get() == -1) {
    return Status::Fail(PStringLog() << "Failed to open loop-control");
  }

  int num = ioctl(ctl_fd.get(), LOOP_CTL_GET_FREE);
  if (num == -1) {
    return Status::Fail(PStringLog() << "Failed LOOP_CTL_GET_FREE");
  }

  out_device = StringPrintf("/dev/block/loop%d", num);

  unique_fd target_fd(open(target.c_str(), O_RDONLY | O_CLOEXEC));
  if (target_fd.get() == -1) {
    return Status::Fail(PStringLog() << "Failed to open " << target);
  }
  unique_fd device_fd(open(out_device.c_str(), O_RDWR | O_CLOEXEC));
  if (device_fd.get() == -1) {
    return Status::Fail(PStringLog() << "Failed to open " << out_device);
  }

  if (ioctl(device_fd.get(), LOOP_SET_FD, target_fd.get()) == -1) {
    return Status::Fail(PStringLog() << "Failed to LOOP_SET_FD");
  }

  struct loop_info64 li;
  memset(&li, 0, sizeof(li));
  strlcpy((char*)li.lo_crypt_name, kApexLoopIdPrefix, LO_NAME_SIZE);
  li.lo_offset = imageOffset;
  li.lo_sizelimit = imageSize;
  if (ioctl(device_fd.get(), LOOP_SET_STATUS64, &li) == -1) {
    return Status::Fail(PStringLog() << "Failed to LOOP_SET_STATUS64");
  }

  // Direct-IO requires the loop device to have the same block size as the
  // underlying filesystem.
  if (ioctl(device_fd.get(), LOOP_SET_BLOCK_SIZE, 4096) == -1) {
    PLOG(WARNING) << "Failed to LOOP_SET_BLOCK_SIZE";
  } else {
    if (ioctl(device_fd.get(), LOOP_SET_DIRECT_IO, 1) == -1) {
      PLOG(WARNING) << "Failed to LOOP_SET_DIRECT_IO";
      // TODO Eventually we'll want to fail on this; right now we can't because
      // not all devices have the necessary kernel patches.
    }
  }

  return Status::Success();
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

static constexpr size_t kLoopDeviceSetupAttempts = 3u;

std::string bytes_to_hex(const uint8_t* bytes, size_t bytes_len) {
  std::ostringstream s;

  s << std::hex << std::setfill('0');
  for (size_t i = 0; i < bytes_len; i++) {
    s << std::setw(2) << static_cast<int>(bytes[i]);
  }
  return s.str();
}

std::string getSalt(const AvbHashtreeDescriptor& desc,
                    const uint8_t* trailingData) {
  const uint8_t* desc_salt = trailingData + desc.partition_name_len;

  return bytes_to_hex(desc_salt, desc.salt_len);
}

std::string getDigest(const AvbHashtreeDescriptor& desc,
                      const uint8_t* trailingData) {
  const uint8_t* desc_digest =
      trailingData + desc.partition_name_len + desc.salt_len;

  return bytes_to_hex(desc_digest, desc.root_digest_len);
}

// Data needed to construct a valid VerityTable
struct ApexVerityData {
  std::unique_ptr<AvbHashtreeDescriptor> desc;
  std::string salt;
  std::string root_digest;
};

std::unique_ptr<DmTable> createVerityTable(const ApexVerityData& verity_data,
                                           const std::string& loop) {
  AvbHashtreeDescriptor* desc = verity_data.desc.get();
  auto table = std::make_unique<DmTable>();

  std::ostringstream hash_algorithm;
  hash_algorithm << desc->hash_algorithm;

  auto target = std::make_unique<DmTargetVerity>(
      0, desc->image_size / 512, desc->dm_verity_version, loop, loop,
      desc->data_block_size, desc->hash_block_size,
      desc->image_size / desc->data_block_size,
      desc->tree_offset / desc->hash_block_size, hash_algorithm.str(),
      verity_data.root_digest, verity_data.salt);

  target->IgnoreZeroBlocks();
  table->AddTarget(std::move(target));

  table->set_readonly(true);

  return table;
}

std::unique_ptr<AvbFooter> getAvbFooter(const ApexFile& apex,
                                        const unique_fd& fd) {
  std::array<uint8_t, AVB_FOOTER_SIZE> footer_data;
  auto footer = std::make_unique<AvbFooter>();

  // The AVB footer is located in the last part of the image
  off_t offset = apex.GetImageSize() + apex.GetImageOffset() - AVB_FOOTER_SIZE;
  int ret = lseek(fd, offset, SEEK_SET);
  if (ret == -1) {
    PLOG(ERROR) << "Couldn't seek to AVB footer.";
    return nullptr;
  }

  ret = read(fd, footer_data.data(), AVB_FOOTER_SIZE);
  if (ret != AVB_FOOTER_SIZE) {
    PLOG(ERROR) << "Couldn't read AVB footer.";
    return nullptr;
  }

  if (!avb_footer_validate_and_byteswap((const AvbFooter*)footer_data.data(),
                                        footer.get())) {
    LOG(ERROR) << "AVB footer verification failed.";
    return nullptr;
  }

  LOG(VERBOSE) << "AVB footer verification successful.";
  return footer;
}

// TODO We'll want to cache the verified key to avoid having to read it every
// time.
bool verifyPublicKey(const uint8_t* key, size_t length,
                     std::string acceptedKeyFile) {
  std::ifstream pubkeyFile(acceptedKeyFile, std::ios::binary | std::ios::ate);
  if (pubkeyFile.bad()) {
    LOG(ERROR) << "Can't open " << acceptedKeyFile;
    return false;
  }

  std::streamsize size = pubkeyFile.tellg();
  if (size < 0) {
    LOG(ERROR) << "Could not get public key length position";
    return false;
  }

  if (static_cast<size_t>(size) != length) {
    LOG(ERROR) << "Public key length (" << std::to_string(size) << ")"
               << " doesn't equal APEX public key length ("
               << std::to_string(length) << ")";
    return false;
  }

  pubkeyFile.seekg(0, std::ios::beg);

  std::string verifiedKey(size, 0);
  pubkeyFile.read(&verifiedKey[0], size);
  if (pubkeyFile.bad()) {
    LOG(ERROR) << "Can't read from " << acceptedKeyFile;
    return false;
  }

  return (memcmp(&verifiedKey[0], key, length) == 0);
}

std::string getPublicKeyFilePath(const ApexFile& apex, const uint8_t* data,
                                 size_t length) {
  size_t keyNameLen;
  const char* keyName = avb_property_lookup(data, length, kApexKeyProp,
      strlen(kApexKeyProp), &keyNameLen);
  if (keyName == nullptr || keyNameLen == 0) {
    LOG(ERROR) << "Cannot find prop \"" << kApexKeyProp << "\" from "
               << apex.GetPath();
    return "";
  }

  std::string keyFilePath(kApexKeyDirectory);
  keyFilePath.append(keyName, keyNameLen);
  std::string canonicalKeyFilePath;
  if (!android::base::Realpath(keyFilePath, &canonicalKeyFilePath)) {
    PLOG(ERROR) << "Failed to get realpath of " << keyFilePath;
    return "";
  }

  if (!android::base::StartsWith(canonicalKeyFilePath, kApexKeyDirectory)) {
    LOG(ERROR) << "Key file " << canonicalKeyFilePath << " is not under "
               << kApexKeyDirectory;
    return "";
  }

  return canonicalKeyFilePath;
}

bool verifyVbMetaSignature(const ApexFile& apex, const uint8_t* data,
                           size_t length) {
  const uint8_t* pk;
  size_t pk_len;
  AvbVBMetaVerifyResult res;

  res = avb_vbmeta_image_verify(data, length, &pk, &pk_len);
  switch (res) {
    case AVB_VBMETA_VERIFY_RESULT_OK:
      break;
    case AVB_VBMETA_VERIFY_RESULT_OK_NOT_SIGNED:
    case AVB_VBMETA_VERIFY_RESULT_HASH_MISMATCH:
    case AVB_VBMETA_VERIFY_RESULT_SIGNATURE_MISMATCH:
      LOG(ERROR) << "Error verifying " << apex.GetPath() << ": "
                 << avb_vbmeta_verify_result_to_string(res);
      return false;
    case AVB_VBMETA_VERIFY_RESULT_INVALID_VBMETA_HEADER:
      LOG(ERROR) << "Error verifying " << apex.GetPath() << ": "
                 << "invalid vbmeta header";
      return false;
    case AVB_VBMETA_VERIFY_RESULT_UNSUPPORTED_VERSION:
      LOG(ERROR) << "Error verifying " << apex.GetPath() << ": "
                 << "unsupported version";
      return false;
    default:
      return false;
  }

  std::string keyFilePath = getPublicKeyFilePath(apex, data, length);
  if (keyFilePath == "") {
    return false;
  }

  // TODO(b/115718846)
  // We need to decide whether we need rollback protection, and whether
  // we can use the rollback protection provided by libavb.
  if (verifyPublicKey(pk, pk_len, keyFilePath)) {
    LOG(VERBOSE) << apex.GetPath() << ": public key matches.";
    return true;
  } else {
    LOG(ERROR) << "Error verifying " << apex.GetPath() << ": "
               << "couldn't verify public key.";
    return false;
  }
}

std::unique_ptr<uint8_t[]> verifyVbMeta(const ApexFile& apex,
                                        const unique_fd& fd,
                                        const AvbFooter& footer) {
  if (footer.vbmeta_size > kVbMetaMaxSize) {
    LOG(ERROR) << "VbMeta size in footer exceeds kVbMetaMaxSize.";
    return nullptr;
  }

  off_t offset = apex.GetImageOffset() + footer.vbmeta_offset;
  std::unique_ptr<uint8_t[]> vbmeta_buf(new uint8_t[footer.vbmeta_size]);

  if (!ReadFullyAtOffset(fd, vbmeta_buf.get(), footer.vbmeta_size, offset)) {
    PLOG(ERROR) << "Couldn't read AVB meta-data.";
    return nullptr;
  }

  if (!verifyVbMetaSignature(apex, vbmeta_buf.get(), footer.vbmeta_size)) {
    return nullptr;
  }

  return vbmeta_buf;
}

const AvbHashtreeDescriptor* findDescriptor(uint8_t* vbmeta_data,
                                            size_t vbmeta_size) {
  const AvbDescriptor** descriptors;
  size_t num_descriptors;

  descriptors =
      avb_descriptor_get_all(vbmeta_data, vbmeta_size, &num_descriptors);

  for (size_t i = 0; i < num_descriptors; i++) {
    AvbDescriptor desc;
    if (!avb_descriptor_validate_and_byteswap(descriptors[i], &desc)) {
      LOG(ERROR) << "Couldn't validate AvbDescriptor.";
      return nullptr;
    }

    if (desc.tag != AVB_DESCRIPTOR_TAG_HASHTREE) {
      // Ignore other descriptors
      continue;
    }

    return (const AvbHashtreeDescriptor*)descriptors[i];
  }

  LOG(ERROR) << "Couldn't find any AVB hashtree descriptors.";
  return nullptr;
}

std::unique_ptr<AvbHashtreeDescriptor> verifyDescriptor(
    const AvbHashtreeDescriptor* desc) {
  auto verifiedDesc = std::make_unique<AvbHashtreeDescriptor>();

  if (!avb_hashtree_descriptor_validate_and_byteswap(desc,
                                                     verifiedDesc.get())) {
    LOG(ERROR) << "Couldn't validate AvbDescriptor.";
    return nullptr;
  }

  return verifiedDesc;
}

std::string createVerityDevice(const std::string& name, const DmTable& table) {
  std::string dev_path;
  DeviceMapper& dm = DeviceMapper::Instance();

  dm.DeleteDevice(name);

  if (!dm.CreateDevice(name, table)) {
    LOG(ERROR) << "Couldn't create verity device.";
    return {};
  }

  if (!dm.GetDmDevicePathByName(name, &dev_path)) {
    LOG(ERROR) << "Couldn't get verity device path!";
    return {};
  }

  return dev_path;
}

// What this function verifies
// 1. The apex file has an AVB footer and that it's valid
// 2. The apex file has a vb metadata structure that is valid
// 3. The vb metadata structure is signed with the correct key
// 4. The vb metadata contains a valid AvbHashTreeDescriptor
//
// If all these steps pass, this function returns an ApexVerityTable
// struct with all the data necessary to create a dm-verity device for this
// APEX.
std::unique_ptr<ApexVerityData> verifyApexVerity(const ApexFile& apex) {
  auto verityData = std::make_unique<ApexVerityData>();

  unique_fd fd(open(apex.GetPath().c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.get() == -1) {
    PLOG(ERROR) << "Failed to open " << apex.GetPath();
    return nullptr;
  }

  std::unique_ptr<AvbFooter> footer = getAvbFooter(apex, fd);
  if (!footer) {
    return nullptr;
  }

  std::unique_ptr<uint8_t[]> vbmeta_data = verifyVbMeta(apex, fd, *footer);
  if (!vbmeta_data) {
    return nullptr;
  }

  const AvbHashtreeDescriptor* descriptor =
      findDescriptor(vbmeta_data.get(), footer->vbmeta_size);
  if (!descriptor) {
    return nullptr;
  }

  verityData->desc = verifyDescriptor(descriptor);
  if (!verityData->desc) {
    return nullptr;
  }

  // This area is now safe to access, because we just verified it
  const uint8_t* trailingData =
      (const uint8_t*)descriptor + sizeof(AvbHashtreeDescriptor);
  verityData->salt = getSalt(*(verityData->desc), trailingData);
  verityData->root_digest = getDigest(*(verityData->desc), trailingData);

  return verityData;
}

void updateSymlink(const std::string& package_name, const std::string& mount_point) {
  std::string link_path = StringPrintf("%s/%s", kApexRoot, package_name.c_str());
  LOG(VERBOSE) << "Creating symlink " << link_path << " with target " << mount_point;
  if (symlink(mount_point.c_str(), link_path.c_str()) != 0) {
    PLOG(ERROR) << "Can't create symlink " << link_path << " with target " << mount_point;
  }
}

}  // namespace

Status mountPackage(const std::string& full_path) {
  LOG(INFO) << "Trying to mount " << full_path;

  StatusOr<std::unique_ptr<ApexFile>> apexFileRes = ApexFile::Open(full_path);
  if (!apexFileRes.Ok()) {
    return apexFileRes.ErrorStatus();
  }
  const std::unique_ptr<ApexFile>& apex = *apexFileRes;

  StatusOr<std::unique_ptr<ApexManifest>> manifestRes =
      ApexManifest::Open(apex->GetManifest());
  if (!manifestRes.Ok()) {
    return manifestRes.ErrorStatus();
  }
  const std::unique_ptr<ApexManifest>& manifest = *manifestRes;
  std::string packageId =
      manifest->GetName() + "@" + std::to_string(manifest->GetVersion());

  std::string loopback;
  for (size_t attempts = 1; ; ++attempts) {
    Status ret = createLoopDevice(full_path, apex->GetImageOffset(),
                                  apex->GetImageSize(), loopback);
    if (ret.Ok()) {
      break;
    }
    if (attempts >= kLoopDeviceSetupAttempts) {
      return Status::Fail(
          StringLog() << "Could not create loop device for " << full_path << ": "
                      << ret.ErrorMessage());
    }
  }
  LOG(VERBOSE) << "Loopback device created: " << loopback;

  auto verityData = verifyApexVerity(*apex);
  if (!verityData) {
    return Status(StringLog() << "Failed to verify Apex Verity data for " << full_path);
  }

  auto verityTable = createVerityTable(*verityData, loopback);
  std::string verityDevice = createVerityDevice(packageId, *verityTable);
  if (verityDevice.empty()) {
    return Status(StringLog() << "Failed to create Apex Verity device " << full_path);
  }

  std::string mountPoint = StringPrintf("%s/%s", kApexRoot, packageId.c_str());
  LOG(VERBOSE) << "Creating mount point: " << mountPoint;
  mkdir(mountPoint.c_str(), 0755);

  if (mount(verityDevice.c_str(), mountPoint.c_str(), "ext4",
            MS_NOATIME | MS_NODEV | MS_NOSUID | MS_DIRSYNC | MS_RDONLY,
            NULL) == 0) {
    LOG(INFO) << "Successfully mounted package " << full_path << " on "
              << mountPoint;

    // TODO: only create symlinks if we are sure we are mounting the latest
    //       version of a package.
    updateSymlink(manifest->GetName(), mountPoint);
    return Status::Success();
  } else {
    Status res = Status::Fail(PStringLog() << "Mounting failed for package " << full_path);
    // Tear down loop device.
    unique_fd fd(open(loopback.c_str(), O_RDWR | O_CLOEXEC));
    if (fd.get() != -1) {
      if (ioctl(fd.get(), LOOP_CLR_FD, 0) < 0) {
        PLOG(WARNING) << "Failed to clean up unused loop device " << loopback;
      }
    } else {
      PLOG(WARNING) << "Failed to open " << loopback
                    << " while attempting to clean up unused loop device";
    }
    return res;
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

void scanPackagesDirAndMount(const char* apex_package_dir) {
  LOG(INFO) << "Scanning " << apex_package_dir << " looking for APEX packages.";
  auto d =
      std::unique_ptr<DIR, int (*)(DIR*)>(opendir(apex_package_dir), closedir);

  if (!d) {
    PLOG(WARNING) << "Package directory " << apex_package_dir
                  << " not found, nothing to do.";
    return;
  }
  struct dirent* dp;
  while ((dp = readdir(d.get())) != NULL) {
    if (dp->d_type != DT_REG || !EndsWith(dp->d_name, kApexPackageSuffix)) {
      continue;
    }
    LOG(INFO) << "Found " << dp->d_name;

    Status res = mountPackage(StringPrintf("%s/%s", apex_package_dir, dp->d_name));
    if (!res.Ok()) {
      LOG(ERROR) << res.ErrorMessage();
    }
  }
}

Status installPackage(const std::string& packageTmpPath) {
  LOG(DEBUG) << "installPackage() for " << packageTmpPath;

  StatusOr<std::unique_ptr<ApexFile>> apexFileRes = ApexFile::Open(packageTmpPath);
  if (!apexFileRes.Ok()) {
    // TODO: Get correct binder error status.
    return apexFileRes.ErrorStatus();
  }
  const std::unique_ptr<ApexFile>& apex = *apexFileRes;

  StatusOr<std::unique_ptr<ApexManifest>> manifestRes =
    ApexManifest::Open(apex->GetManifest());
  if (!manifestRes.Ok()) {
    // TODO: Get correct binder error status.
    return manifestRes.ErrorStatus();
  }
  const std::unique_ptr<ApexManifest>& manifest = *manifestRes;
  std::string packageId =
      manifest->GetName() + "@" + std::to_string(manifest->GetVersion());

  std::string destPath = StringPrintf("%s/%s%s",
                                      kApexPackageDataDir,
                                      packageId.c_str(),
                                      kApexPackageSuffix);
  if (rename(packageTmpPath.c_str(), destPath.c_str()) != 0) {
    // TODO: Get correct binder error status.
    return Status::Fail(
        PStringLog() << "Unable to rename " << packageTmpPath << " to " << destPath);
  }
  LOG(DEBUG) << "Success renaming " << packageTmpPath << " to " << destPath;
  return Status::Success();
}

}  // namespace apex
}  // namespace android
