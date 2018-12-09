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

#include "apexd_preinstall.h"

#include <algorithm>
#include <vector>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>

#include "apex_file.h"
#include "apexd.h"
#include "apexd_private.h"
#include "apexd_utils.h"
#include "string_log.h"

namespace android {
namespace apex {

namespace {

void PseudoCloseDescriptors() {
  int write_fd = open("/dev/null", O_WRONLY);
  int read_fd = open("/dev/zero", O_RDONLY);
  if (write_fd == -1 || read_fd == -1) {
    PLOG(FATAL) << "Error opening fds " << write_fd << " " << read_fd;
  }
  auto dup_or_close = [](int new_fd, int old_fd) {
    int rc = TEMP_FAILURE_RETRY(dup2(new_fd, old_fd));
    if (rc != 0) {
      if (errno != EBADF) {
        rc = close(old_fd);
      }
    }
  };
  dup_or_close(read_fd, STDIN_FILENO);
  dup_or_close(write_fd, STDOUT_FILENO);
  dup_or_close(write_fd, STDERR_FILENO);
}

}  // namespace

Status StagePreInstall(const ApexFile& apex) {
  LOG(VERBOSE) << "Preinstall for " << apex.GetPath();

  // 1) Mount the package, if necessary.
  auto mount_guard = android::base::make_scope_guard([&]() {
    // TODO: Unmount code here.
  });

  std::string mount_point =
      apexd_private::GetPackageMountPoint(apex.GetManifest());
  apexd_private::MountedApexData apex_data("", apex.GetPath());

  if (!apexd_private::IsMounted(apex.GetManifest().GetName(), apex.GetPath())) {
    Status mountStatus =
        apexd_private::MountPackage(apex, mount_point, &apex_data);
    if (!mountStatus.Ok()) {
      // Not mounted yet, skip warnings.
      mount_guard.Disable();
      return mountStatus;
    }
  } else {
    // Already mounted, don't unmount at the end.
    mount_guard.Disable();
  }

  // 2) Create invocation args.
  std::vector<std::string> args{
      "/system/bin/apexd",
      "--pre-install",
      apex.GetPath(),
  };
  std::string error_msg;
  int res = ForkAndRun(args, &error_msg);
  return res == 0 ? Status::Success() : Status::Fail(error_msg);
}

int RunPreInstall(char** in_argv) {
  // 1) Close all file descriptors. They are coming from the caller, we do not
  // want to pass them on across our fork/exec into a different domain.
  PseudoCloseDescriptors();

  // 2) Unshare.
  if (unshare(CLONE_NEWNS) != 0) {
    PLOG(ERROR) << "Failed to unshare() for apex pre-install.";
    _exit(200);
  }

  // 3) Make /apex private, so that our changes don't propagate.
  if (mount("", kApexRoot, nullptr, MS_PRIVATE, nullptr) != 0) {
    PLOG(ERROR) << "Failed to mount private.";
    _exit(201);
  }

  std::string apex = in_argv[2];
  std::string pre_install_hook;
  std::string mount_point;
  std::string active_point;
  {
    StatusOr<ApexFile> apex_file = ApexFile::Open(apex);
    if (!apex_file.Ok()) {
      LOG(ERROR) << "Could not open apex " << apex
                 << " for pre-install: " << apex_file.ErrorMessage();
      _exit(202);
    }
    const ApexManifest& manifest = apex_file->GetManifest();
    pre_install_hook = manifest.GetPreInstallHook();
    CHECK_NE(std::string(""), pre_install_hook);

    mount_point = apexd_private::GetPackageMountPoint(manifest);
    active_point = apexd_private::GetActiveMountPoint(manifest);
  }

  // 4) Activate the new apex.
  Status bind_status = apexd_private::BindMount(active_point, mount_point);
  if (!bind_status.Ok()) {
    LOG(ERROR) << "Failed to bind-mount " << mount_point << " to "
               << active_point << ": " << bind_status.ErrorMessage();
    _exit(203);
  }

  // 5) Run the hook.
  std::string pre_install_path = active_point + "/" + pre_install_hook;

  // For now, just run sh. But this probably needs to run the new linker.
  std::vector<std::string> args{
      "/system/bin/sh",
      "-c",
      pre_install_path,
  };
  std::vector<const char*> argv;
  argv.resize(args.size() + 1, nullptr);
  std::transform(args.begin(), args.end(), argv.begin(),
                 [](const std::string& in) { return in.c_str(); });

  LOG(ERROR) << "execv of " << android::base::Join(args, " ");

  execv(argv[0], const_cast<char**>(argv.data()));
  PLOG(ERROR) << "execv of " << android::base::Join(args, " ") << " failed";
  _exit(204);
}

}  // namespace apex
}  // namespace android
