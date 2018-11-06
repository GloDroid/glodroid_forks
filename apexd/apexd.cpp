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
#include "status_or.h"
#include "string_log.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
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
static constexpr const char* kApexLoopIdPrefix = "apex:";
static constexpr const char* kApexKeyDirectory = "/system/etc/security/apex/";
static constexpr const char* kApexKeyProp = "apex.key";
// These should be in-sync with system/sepolicy/public/property_contexts
static constexpr const char* kApexStatusSysprop = "apexd.status";
static constexpr const char* kApexStatusStarting = "starting";
static constexpr const char* kApexStatusReady = "ready";

static constexpr int kVbMetaMaxSize = 64 * 1024;

struct LoopbackDeviceUniqueFd {
  unique_fd device_fd;
  std::string name;

  LoopbackDeviceUniqueFd() {}
  LoopbackDeviceUniqueFd(unique_fd&& fd, const std::string& name)
      : device_fd(std::move(fd)), name(name) {}

  LoopbackDeviceUniqueFd(LoopbackDeviceUniqueFd&& fd) noexcept
      : device_fd(std::move(fd.device_fd)), name(fd.name) {}
  LoopbackDeviceUniqueFd& operator=(LoopbackDeviceUniqueFd&& other) noexcept {
    MaybeCloseBad();
    device_fd = std::move(other.device_fd);
    name = std::move(other.name);
    return *this;
  }

  ~LoopbackDeviceUniqueFd() { MaybeCloseBad(); }

  void MaybeCloseBad() {
    if (device_fd.get() != -1) {
      // Disassociate any files.
      if (ioctl(device_fd.get(), LOOP_CLR_FD) == -1) {
        PLOG(ERROR) << "Unable to clear fd for loopback device";
      }
    }
  }

  void CloseGood() { device_fd.reset(-1); }

  int get() { return device_fd.get(); }
};

StatusOr<LoopbackDeviceUniqueFd> createLoopDevice(const std::string& target,
                                                  const int32_t imageOffset,
                                                  const size_t imageSize) {
  using Failed = StatusOr<LoopbackDeviceUniqueFd>;
  unique_fd ctl_fd(open("/dev/loop-control", O_RDWR | O_CLOEXEC));
  if (ctl_fd.get() == -1) {
    return Failed::MakeError(PStringLog() << "Failed to open loop-control");
  }

  int num = ioctl(ctl_fd.get(), LOOP_CTL_GET_FREE);
  if (num == -1) {
    return Failed::MakeError(PStringLog() << "Failed LOOP_CTL_GET_FREE");
  }

  std::string device = StringPrintf("/dev/block/loop%d", num);

  unique_fd target_fd(open(target.c_str(), O_RDONLY | O_CLOEXEC));
  if (target_fd.get() == -1) {
    return Failed::MakeError(PStringLog() << "Failed to open " << target);
  }
  LoopbackDeviceUniqueFd device_fd(
      unique_fd(open(device.c_str(), O_RDWR | O_CLOEXEC)), device);
  if (device_fd.get() == -1) {
    return Failed::MakeError(PStringLog() << "Failed to open " << device);
  }

  if (ioctl(device_fd.get(), LOOP_SET_FD, target_fd.get()) == -1) {
    return Failed::MakeError(PStringLog() << "Failed to LOOP_SET_FD");
  }

  struct loop_info64 li;
  memset(&li, 0, sizeof(li));
  strlcpy((char*)li.lo_crypt_name, kApexLoopIdPrefix, LO_NAME_SIZE);
  li.lo_offset = imageOffset;
  li.lo_sizelimit = imageSize;
  if (ioctl(device_fd.get(), LOOP_SET_STATUS64, &li) == -1) {
    return Failed::MakeError(PStringLog() << "Failed to LOOP_SET_STATUS64");
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

  return StatusOr<LoopbackDeviceUniqueFd>(std::move(device_fd));
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

StatusOr<std::unique_ptr<AvbFooter>> getAvbFooter(const ApexFile& apex,
                                                  const unique_fd& fd) {
  std::array<uint8_t, AVB_FOOTER_SIZE> footer_data;
  auto footer = std::make_unique<AvbFooter>();

  // The AVB footer is located in the last part of the image
  off_t offset = apex.GetImageSize() + apex.GetImageOffset() - AVB_FOOTER_SIZE;
  int ret = lseek(fd, offset, SEEK_SET);
  if (ret == -1) {
    return StatusOr<std::unique_ptr<AvbFooter>>::MakeError(
        PStringLog() << "Couldn't seek to AVB footer.");
  }

  ret = read(fd, footer_data.data(), AVB_FOOTER_SIZE);
  if (ret != AVB_FOOTER_SIZE) {
    return StatusOr<std::unique_ptr<AvbFooter>>::MakeError(
        PStringLog() << "Couldn't read AVB footer.");
  }

  if (!avb_footer_validate_and_byteswap((const AvbFooter*)footer_data.data(),
                                        footer.get())) {
    return StatusOr<std::unique_ptr<AvbFooter>>::MakeError(
        StringLog() << "AVB footer verification failed.");
  }

  LOG(VERBOSE) << "AVB footer verification successful.";
  return StatusOr<std::unique_ptr<AvbFooter>>(std::move(footer));
}

// TODO We'll want to cache the verified key to avoid having to read it every
// time.
Status verifyPublicKey(const uint8_t* key, size_t length,
                       std::string acceptedKeyFile) {
  std::ifstream pubkeyFile(acceptedKeyFile, std::ios::binary | std::ios::ate);
  if (pubkeyFile.bad()) {
    return Status::Fail(StringLog() << "Can't open " << acceptedKeyFile);
  }

  std::streamsize size = pubkeyFile.tellg();
  if (size < 0) {
    return Status::Fail(StringLog()
                        << "Could not get public key length position");
  }

  if (static_cast<size_t>(size) != length) {
    return Status::Fail(StringLog()
                        << "Public key length (" << std::to_string(size) << ")"
                        << " doesn't equal APEX public key length ("
                        << std::to_string(length) << ")");
  }

  pubkeyFile.seekg(0, std::ios::beg);

  std::string verifiedKey(size, 0);
  pubkeyFile.read(&verifiedKey[0], size);
  if (pubkeyFile.bad()) {
    return Status::Fail(StringLog() << "Can't read from " << acceptedKeyFile);
  }

  if (memcmp(&verifiedKey[0], key, length) != 0) {
    return Status::Fail("Failed to compare verified key with key");
  }
  return Status::Success();
}

StatusOr<std::string> getPublicKeyFilePath(const ApexFile& apex,
                                           const uint8_t* data, size_t length) {
  size_t keyNameLen;
  const char* keyName = avb_property_lookup(data, length, kApexKeyProp,
                                            strlen(kApexKeyProp), &keyNameLen);
  if (keyName == nullptr || keyNameLen == 0) {
    return StatusOr<std::string>::MakeError(
        StringLog() << "Cannot find prop \"" << kApexKeyProp << "\" from "
                    << apex.GetPath());
  }

  std::string keyFilePath(kApexKeyDirectory);
  keyFilePath.append(keyName, keyNameLen);
  std::string canonicalKeyFilePath;
  if (!android::base::Realpath(keyFilePath, &canonicalKeyFilePath)) {
    return StatusOr<std::string>::MakeError(
        PStringLog() << "Failed to get realpath of " << keyFilePath);
  }

  if (!android::base::StartsWith(canonicalKeyFilePath, kApexKeyDirectory)) {
    return StatusOr<std::string>::MakeError(
        StringLog() << "Key file " << canonicalKeyFilePath << " is not under "
                    << kApexKeyDirectory);
  }

  return StatusOr<std::string>(canonicalKeyFilePath);
}

Status verifyVbMetaSignature(const ApexFile& apex, const uint8_t* data,
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
      return Status::Fail(StringLog()
                          << "Error verifying " << apex.GetPath() << ": "
                          << avb_vbmeta_verify_result_to_string(res));
    case AVB_VBMETA_VERIFY_RESULT_INVALID_VBMETA_HEADER:
      return Status::Fail(StringLog()
                          << "Error verifying " << apex.GetPath() << ": "
                          << "invalid vbmeta header");
    case AVB_VBMETA_VERIFY_RESULT_UNSUPPORTED_VERSION:
      return Status::Fail(StringLog()
                          << "Error verifying " << apex.GetPath() << ": "
                          << "unsupported version");
    default:
      return Status::Fail("Unknown vmbeta_image_verify return value");
  }

  StatusOr<std::string> keyFilePath = getPublicKeyFilePath(apex, data, length);
  if (!keyFilePath.Ok()) {
    return keyFilePath.ErrorStatus();
  }

  // TODO(b/115718846)
  // We need to decide whether we need rollback protection, and whether
  // we can use the rollback protection provided by libavb.
  Status st = verifyPublicKey(pk, pk_len, *keyFilePath);
  if (st.Ok()) {
    LOG(VERBOSE) << apex.GetPath() << ": public key matches.";
    return st;
  }
  return Status::Fail(StringLog()
                      << "Error verifying " << apex.GetPath() << ": "
                      << "couldn't verify public key: " << st.ErrorMessage());
}

StatusOr<std::unique_ptr<uint8_t[]>> verifyVbMeta(const ApexFile& apex,
                                                  const unique_fd& fd,
                                                  const AvbFooter& footer) {
  if (footer.vbmeta_size > kVbMetaMaxSize) {
    return StatusOr<std::unique_ptr<uint8_t[]>>::MakeError(
        "VbMeta size in footer exceeds kVbMetaMaxSize.");
  }

  off_t offset = apex.GetImageOffset() + footer.vbmeta_offset;
  std::unique_ptr<uint8_t[]> vbmeta_buf(new uint8_t[footer.vbmeta_size]);

  if (!ReadFullyAtOffset(fd, vbmeta_buf.get(), footer.vbmeta_size, offset)) {
    return StatusOr<std::unique_ptr<uint8_t[]>>::MakeError(
        PStringLog() << "Couldn't read AVB meta-data.");
  }

  Status st = verifyVbMetaSignature(apex, vbmeta_buf.get(), footer.vbmeta_size);
  if (!st.Ok()) {
    return StatusOr<std::unique_ptr<uint8_t[]>>::MakeError(st.ErrorMessage());
  }

  return StatusOr<std::unique_ptr<uint8_t[]>>(std::move(vbmeta_buf));
}

StatusOr<const AvbHashtreeDescriptor*> findDescriptor(uint8_t* vbmeta_data,
                                                      size_t vbmeta_size) {
  const AvbDescriptor** descriptors;
  size_t num_descriptors;

  descriptors =
      avb_descriptor_get_all(vbmeta_data, vbmeta_size, &num_descriptors);

  for (size_t i = 0; i < num_descriptors; i++) {
    AvbDescriptor desc;
    if (!avb_descriptor_validate_and_byteswap(descriptors[i], &desc)) {
      return StatusOr<const AvbHashtreeDescriptor*>::MakeError(
          "Couldn't validate AvbDescriptor.");
    }

    if (desc.tag != AVB_DESCRIPTOR_TAG_HASHTREE) {
      // Ignore other descriptors
      continue;
    }

    return StatusOr<const AvbHashtreeDescriptor*>(
        (const AvbHashtreeDescriptor*)descriptors[i]);
  }

  return StatusOr<const AvbHashtreeDescriptor*>::MakeError(
      "Couldn't find any AVB hashtree descriptors.");
}

StatusOr<std::unique_ptr<AvbHashtreeDescriptor>> verifyDescriptor(
    const AvbHashtreeDescriptor* desc) {
  auto verifiedDesc = std::make_unique<AvbHashtreeDescriptor>();

  if (!avb_hashtree_descriptor_validate_and_byteswap(desc,
                                                     verifiedDesc.get())) {
    StatusOr<std::unique_ptr<AvbHashtreeDescriptor>>::MakeError(
        "Couldn't validate AvbDescriptor.");
  }

  return StatusOr<std::unique_ptr<AvbHashtreeDescriptor>>(
      std::move(verifiedDesc));
}

class DmVerityDevice {
 public:
  DmVerityDevice() : cleared_(true) {}
  DmVerityDevice(const std::string& name) : name_(name), cleared_(false) {}
  DmVerityDevice(const std::string& name, const std::string& dev_path)
      : name_(name), dev_path_(dev_path), cleared_(false) {}

  DmVerityDevice(DmVerityDevice&& other)
      : name_(other.name_),
        dev_path_(other.dev_path_),
        cleared_(other.cleared_) {
    other.cleared_ = true;
  }

  ~DmVerityDevice() {
    if (!cleared_) {
      DeviceMapper& dm = DeviceMapper::Instance();
      dm.DeleteDevice(name_);
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

StatusOr<DmVerityDevice> createVerityDevice(const std::string& name,
                                            const DmTable& table) {
  DeviceMapper& dm = DeviceMapper::Instance();

  dm.DeleteDevice(name);

  if (!dm.CreateDevice(name, table)) {
    return StatusOr<DmVerityDevice>::MakeError(
        "Couldn't create verity device.");
  }
  DmVerityDevice dev(name);

  std::string dev_path;
  if (!dm.GetDmDevicePathByName(name, &dev_path)) {
    return StatusOr<DmVerityDevice>::MakeError(
        "Couldn't get verity device path!");
  }
  dev.SetDevPath(dev_path);

  return StatusOr<DmVerityDevice>(std::move(dev));
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
StatusOr<std::unique_ptr<ApexVerityData>> verifyApexVerity(
    const ApexFile& apex) {
  auto verityData = std::make_unique<ApexVerityData>();

  unique_fd fd(open(apex.GetPath().c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.get() == -1) {
    return StatusOr<std::unique_ptr<ApexVerityData>>::MakeError(
        PStringLog() << "Failed to open " << apex.GetPath());
  }

  StatusOr<std::unique_ptr<AvbFooter>> footer = getAvbFooter(apex, fd);
  if (!footer.Ok()) {
    return StatusOr<std::unique_ptr<ApexVerityData>>::MakeError(
        footer.ErrorMessage());
  }

  StatusOr<std::unique_ptr<uint8_t[]>> vbmeta_data =
      verifyVbMeta(apex, fd, **footer);
  if (!vbmeta_data.Ok()) {
    return StatusOr<std::unique_ptr<ApexVerityData>>::MakeError(
        vbmeta_data.ErrorMessage());
  }

  StatusOr<const AvbHashtreeDescriptor*> descriptor =
      findDescriptor(vbmeta_data->get(), (*footer)->vbmeta_size);
  if (!descriptor.Ok()) {
    return StatusOr<std::unique_ptr<ApexVerityData>>::MakeError(
        descriptor.ErrorMessage());
  }

  StatusOr<std::unique_ptr<AvbHashtreeDescriptor>> verifiedDescriptor =
      verifyDescriptor(*descriptor);
  if (!verifiedDescriptor.Ok()) {
    return StatusOr<std::unique_ptr<ApexVerityData>>::MakeError(
        verifiedDescriptor.ErrorMessage());
  }
  verityData->desc = std::move(*verifiedDescriptor);

  // This area is now safe to access, because we just verified it
  const uint8_t* trailingData =
      (const uint8_t*)*descriptor + sizeof(AvbHashtreeDescriptor);
  verityData->salt = getSalt(*(verityData->desc), trailingData);
  verityData->root_digest = getDigest(*(verityData->desc), trailingData);

  return StatusOr<std::unique_ptr<ApexVerityData>>(std::move(verityData));
}

void updateSymlink(const std::string& package_name,
                   const std::string& mount_point) {
  std::string link_path =
      StringPrintf("%s/%s", kApexRoot, package_name.c_str());
  LOG(VERBOSE) << "Creating symlink " << link_path << " with target "
               << mount_point;
  if (symlink(mount_point.c_str(), link_path.c_str()) != 0) {
    PLOG(ERROR) << "Can't create symlink " << link_path << " with target "
                << mount_point;
  }
}

}  // namespace

Status activatePackage(const std::string& full_path) {
  LOG(INFO) << "Trying to activate " << full_path;

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

  LoopbackDeviceUniqueFd loopbackDevice;
  for (size_t attempts = 1;; ++attempts) {
    StatusOr<LoopbackDeviceUniqueFd> ret = createLoopDevice(
        full_path, apex->GetImageOffset(), apex->GetImageSize());
    if (ret.Ok()) {
      loopbackDevice = std::move(*ret);
      break;
    }
    if (attempts >= kLoopDeviceSetupAttempts) {
      return Status::Fail(StringLog()
                          << "Could not create loop device for " << full_path
                          << ": " << ret.ErrorMessage());
    }
  }
  LOG(VERBOSE) << "Loopback device created: " << loopbackDevice.name;

  auto verityData = verifyApexVerity(*apex);
  if (!verityData.Ok()) {
    return Status(StringLog()
                  << "Failed to verify Apex Verity data for " << full_path
                  << ": " << verityData.ErrorMessage());
  }

  auto verityTable = createVerityTable(**verityData, loopbackDevice.name);
  StatusOr<DmVerityDevice> verityDevRes =
      createVerityDevice(packageId, *verityTable);
  if (!verityDevRes.Ok()) {
    return Status(StringLog()
                  << "Failed to create Apex Verity device " << full_path << ": "
                  << verityDevRes.ErrorMessage());
  }
  DmVerityDevice verityDev = std::move(*verityDevRes);

  std::string mountPoint = StringPrintf("%s/%s", kApexRoot, packageId.c_str());
  LOG(VERBOSE) << "Creating mount point: " << mountPoint;
  mkdir(mountPoint.c_str(), 0755);

  if (mount(verityDev.GetDevPath().c_str(), mountPoint.c_str(), "ext4",
            MS_NOATIME | MS_NODEV | MS_NOSUID | MS_DIRSYNC | MS_RDONLY,
            NULL) == 0) {
    LOG(INFO) << "Successfully mounted package " << full_path << " on "
              << mountPoint;

    // TODO: only create symlinks if we are sure we are mounting the latest
    //       version of a package.
    updateSymlink(manifest->GetName(), mountPoint);

    // Time to accept the temporaries as good.
    verityDev.Release();
    loopbackDevice.CloseGood();

    return Status::Success();
  }
  return Status::Fail(PStringLog()
                      << "Mounting failed for package " << full_path);
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

void scanPackagesDirAndActivate(const char* apex_package_dir) {
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

    Status res =
        activatePackage(StringPrintf("%s/%s", apex_package_dir, dp->d_name));
    if (!res.Ok()) {
      LOG(ERROR) << res.ErrorMessage();
    }
  }
}

Status installPackage(const std::string& packageTmpPath) {
  LOG(DEBUG) << "installPackage() for " << packageTmpPath;

  StatusOr<std::unique_ptr<ApexFile>> apexFileRes =
      ApexFile::Open(packageTmpPath);
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

  std::string destPath = StringPrintf("%s/%s%s", kApexPackageDataDir,
                                      packageId.c_str(), kApexPackageSuffix);
  if (rename(packageTmpPath.c_str(), destPath.c_str()) != 0) {
    // TODO: Get correct binder error status.
    return Status::Fail(PStringLog() << "Unable to rename " << packageTmpPath
                                     << " to " << destPath);
  }
  LOG(DEBUG) << "Success renaming " << packageTmpPath << " to " << destPath;
  return Status::Success();
}

void onStart() {
  if (!android::base::SetProperty(kApexStatusSysprop, kApexStatusStarting)) {
    PLOG(ERROR) << "Failed to set " << kApexStatusSysprop << " to "
                << kApexStatusStarting;
  }
}

void onAllPackagesReady() {
  // Set a system property to let other components to know that APEXs are
  // correctly mounted and ready to be used. Before using any file from APEXs,
  // they can query this system property to ensure that they are okay to
  // access. Or they may have a on-property trigger to delay a task until
  // APEXs become ready.
  if (!android::base::SetProperty(kApexStatusSysprop, kApexStatusReady)) {
    PLOG(ERROR) << "Failed to set " << kApexStatusSysprop << " to "
                << kApexStatusReady;
  }
}

}  // namespace apex
}  // namespace android
