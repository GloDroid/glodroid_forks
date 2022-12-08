# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

AOSPEXT_PROJECT_NAME := MMRADIO

ifneq ($(filter true, $(BOARD_BUILD_AOSPEXT_MMRADIO)),)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcutils \
    libutils \
    libbinder_ndk \
    android.hardware.radio-V1-ndk \
    android.hardware.radio.config-V1-ndk \
    android.hardware.radio.data-V1-ndk \
    android.hardware.radio.messaging-V1-ndk \
    android.hardware.radio.modem-V1-ndk \
    android.hardware.radio.network-V1-ndk \
    android.hardware.radio.sim-V1-ndk \
    android.hardware.radio.voice-V1-ndk \

MESON_GEN_PKGCONFIGS := \
    base \
    cutils \
    utils \
    binder_ndk \
    android.hardware.radio-V1-ndk \
    android.hardware.radio.config-V1-ndk \
    android.hardware.radio.data-V1-ndk \
    android.hardware.radio.messaging-V1-ndk \
    android.hardware.radio.modem-V1-ndk \
    android.hardware.radio.network-V1-ndk \
    android.hardware.radio.sim-V1-ndk \
    android.hardware.radio.voice-V1-ndk \

MESON_BUILD_ARGUMENTS := \

# Format: TYPE:REL_PATH_TO_INSTALL_ARTIFACT:VENDOR_SUBDIR:MODULE_NAME:SYMLINK_SUFFIX
# TYPE one of: lib, bin, etc
AOSPEXT_GEN_TARGETS := \
    bin:android.hardware.mm-radio-service:hw:android.hardware.mm-radio-service: \
    $(BOARD_LIBGUDEV_EXTRA_TARGETS)

# Build first ARCH only
LOCAL_MULTILIB := first
include $(LOCAL_PATH)/meson_cross.mk
AOSPEXT_TARGETS_DEP:=$(MESON_GEN_FILES_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(AOSPEXT_TARGETS_DEP))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=
include $(LOCAL_PATH)/aospext_gen_targets.mk

#-------------------------------------------------------------------------------

endif # BOARD_BUILD_AOSPEXT_MMRADIO
