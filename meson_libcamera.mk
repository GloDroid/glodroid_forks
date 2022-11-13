# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

AOSPEXT_PROJECT_NAME := LIBCAMERA

ifneq ($(filter true, $(BOARD_BUILD_AOSPEXT_LIBCAMERA)),)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libc libexif libjpeg libdl libudev libevent libcrypto
MESON_GEN_PKGCONFIGS := libexif libjpeg dl libudev libevent_pthreads libcrypto

MESON_BUILD_ARGUMENTS := \
    -Dwerror=false                                                           \
    -Dandroid=enabled                                                        \
    -Dipas=$(subst $(space),$(comma),$(BOARD_LIBCAMERA_IPAS))                \
    -Dpipelines=$(subst $(space),$(comma),$(BOARD_LIBCAMERA_PIPELINES))      \
    -Dsysconfdir=/vendor/etc                                                 \
    -Dtest=false                                                             \
    -Dlc-compliance=disabled                                                 \
    -Dcam=enabled                                                            \

# Format: TYPE:REL_PATH_TO_INSTALL_ARTIFACT:VENDOR_SUBDIR:MODULE_NAME:SYMLINK_SUFFIX
# TYPE one of: lib, bin, etc
AOSPEXT_GEN_TARGETS := \
    lib:libcamera.so::libcamera:              \
    lib:libcamera-base.so::libcamera-base:    \
    lib:libcamera-hal.so:hw:camera.libcamera: \
    bin:cam::libcamera-cam:                   \
    $(BOARD_LIBCAMERA_EXTRA_TARGETS)

LOCAL_MULTILIB := first
include $(LOCAL_PATH)/meson_cross.mk
AOSPEXT_TARGETS_DEP:=$(MESON_GEN_FILES_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(AOSPEXT_TARGETS_DEP))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=$(AOSPEXT_PROJECT_INSTALL_DIR)/usr/local/include/libcamera
include $(LOCAL_PATH)/aospext_gen_targets.mk

#-------------------------------------------------------------------------------

endif # BOARD_BUILD_AOSPEXT_LIBCAMERA
