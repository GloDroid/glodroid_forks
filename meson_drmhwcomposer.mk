# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

AOSPEXT_PROJECT_NAME := DRMHWCOMPOSER

ifneq ($(filter true, $(BOARD_BUILD_AOSPEXT_DRMHWCOMPOSER)),)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libcutils libdrm libhardware libhidlbase liblog libsync libui libutils
MESON_GEN_PKGCONFIGS := cutils drm hardware hidlbase log sync ui utils

MESON_BUILD_ARGUMENTS := \

# Format: TYPE:REL_PATH_TO_INSTALL_ARTIFACT:VENDOR_SUBDIR:MODULE_NAME:SYMLINK_SUFFIX
# TYPE one of: lib, bin, etc
AOSPEXT_GEN_TARGETS := \
    lib:hw/hwcomposer.drm.so:hw:hwcomposer.drm2: \
    $(BOARD_LIBGUDEV_EXTRA_TARGETS)

# Build first ARCH only
LOCAL_MULTILIB := first
include $(LOCAL_PATH)/meson_cross.mk
AOSPEXT_TARGETS_DEP:=$(MESON_GEN_FILES_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(AOSPEXT_TARGETS_DEP))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=
include $(LOCAL_PATH)/aospext_gen_targets.mk

#-------------------------------------------------------------------------------

endif # BOARD_BUILD_AOSPEXT_DRMHWCOMPOSER
