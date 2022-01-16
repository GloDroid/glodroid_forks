/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "hwc-uevent-listener"

#include "UEventListener.h"

#include <linux/netlink.h>
#include <sys/socket.h>
#include <xf86drm.h>

#include <cassert>
#include <cerrno>
#include <cstring>

#include "utils/log.h"

/* Originally defined in system/core/libsystem/include/system/graphics.h */
#define HAL_PRIORITY_URGENT_DISPLAY (-8)

namespace android {

UEventListener::UEventListener()
    : Worker("uevent-listener", HAL_PRIORITY_URGENT_DISPLAY){};

int UEventListener::Init() {
  uevent_fd_ = UniqueFd(
      socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT));
  if (!uevent_fd_) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Fixme
    ALOGE("Failed to open uevent socket: %s", strerror(errno));
    return -errno;
  }

  struct sockaddr_nl addr {};
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = 0;
  addr.nl_groups = 0xFFFFFFFF;

  int ret = bind(uevent_fd_.Get(), (struct sockaddr *)&addr, sizeof(addr));
  if (ret) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Fixme
    ALOGE("Failed to bind uevent socket: %s", strerror(errno));
    return -errno;
  }

  return InitWorker();
}

void UEventListener::Routine() {
  char buffer[1024];
  ssize_t ret = 0;

  while (true) {
    ret = read(uevent_fd_.Get(), &buffer, sizeof(buffer));
    if (ret == 0)
      return;

    if (ret < 0) {
      ALOGE("Got error reading uevent %zd", ret);
      return;
    }

    if (!hotplug_handler_)
      continue;

    bool drm_event = false;
    bool hotplug_event = false;
    for (uint32_t i = 0; (ssize_t)i < ret;) {
      char *event = buffer + i;
      if (strcmp(event, "DEVTYPE=drm_minor") != 0)
        drm_event = true;
      else if (strcmp(event, "HOTPLUG=1") != 0)
        hotplug_event = true;

      i += strlen(event) + 1;
    }

    if (drm_event && hotplug_event && hotplug_handler_) {
      constexpr useconds_t delay_after_uevent_us = 200000;
      /* We need some delay to ensure DrmConnector::UpdateModes() will query
       * correct modes list, otherwise at least RPI4 board may report 0 modes */
      usleep(delay_after_uevent_us);
      hotplug_handler_();
    }
  }
}
}  // namespace android
