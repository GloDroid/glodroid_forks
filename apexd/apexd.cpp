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
#include <libavb/libavb.h>
#include <libdm/dm.h>
#include <libdm/dm_table.h>
#include <libdm/dm_target.h>

#include <utils/String16.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/loop.h>
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
using android::dm::DeviceMapper;
using android::dm::DmTable;
using android::dm::DmTargetVerity;

namespace android {
namespace apex {

static constexpr const char* kApexPackageDir = "/data/apex";
static constexpr const char* kApexPackageSuffix = ".apex";
static constexpr const char* kApexRoot = "/mnt/apex";
static constexpr const char* kApexLoopIdPrefix = "apex:";

// For now, we assume a single key in a known location
static constexpr const char* kApexDebugKeyFile =
    "/system/etc/security/apex/apex_debug_key";

static constexpr int kVbMetaMaxSize = 64 * 1024;

status_t createLoopDevice(const std::string& target, const int32_t imageOffset,
                          const size_t imageSize, std::string& out_device) {
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

  unique_fd target_fd(open(target.c_str(), O_RDONLY | O_CLOEXEC));
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

  int use_dio = 1;
  if (ioctl(device_fd.get(), LOOP_SET_DIRECT_IO, &use_dio) == -1) {
    PLOG(WARNING) << "Failed to LOOP_SET_DIRECT_IO";
    // TODO decide if we want to fail on this or not.
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

  if (size != length) {
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

bool verifyPublicKey(const uint8_t* key, size_t length) {
#ifdef ALLOW_DEBUG_KEY
  return verifyPublicKey(key, length, kApexDebugKeyFile);
#else
  // This code will be changed significantly; for now, play it safe.
  (void) key;
  (void) length;
  (void) kApexDebugKeyFile;
  return false;
#endif
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

  // TODO(b/115718846)
  // We need to decide whether we need rollback protection, and whether
  // we can use the rollback protection provided by libavb.
  if (verifyPublicKey(pk, pk_len)) {
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

  int ret = lseek(fd, offset, SEEK_SET);
  if (ret != offset) {
    PLOG(ERROR) << "Couldn't seek to AVB meta-data.";
    return nullptr;
  }

  ret = read(fd, vbmeta_buf.get(), footer.vbmeta_size);
  if (ret != footer.vbmeta_size) {
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

void installPackage(const std::string& full_path) {
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
  std::string packageId =
      manifest->GetName() + "@" + std::to_string(manifest->GetVersion());

  std::string loopback;
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

  auto verityData = verifyApexVerity(*apex);
  if (!verityData) {
    return;
  }

  auto verityTable = createVerityTable(*verityData, loopback);
  std::string verityDevice = createVerityDevice(packageId, *verityTable);
  if (verityDevice.empty()) {
    return;
  }

  std::string mountPoint = StringPrintf("%s/%s", kApexRoot, packageId.c_str());
  LOG(VERBOSE) << "Creating mount point: " << mountPoint;
  mkdir(mountPoint.c_str(), 0755);

  if (mount(verityDevice.c_str(), mountPoint.c_str(), "ext4",
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
