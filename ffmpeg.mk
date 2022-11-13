# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

AOSPEXT_PROJECT_NAME := FFMPEG

ifneq ($(filter true, $(BOARD_BUILD_AOSPEXT_FFMPEG)),)

LOCAL_PATH := $(call my-dir)

LIBDRM_VERSION = $(shell cat external/libdrm/meson.build | grep -o "\<version\>\s*:\s*'\w*\.\w*\.\w*'" | grep -o "\w*\.\w*\.\w*" | head -1)

# Format: TYPE:REL_PATH_TO_INSTALL_ARTIFACT:VENDOR_SUBDIR:MODULE_NAME:SYMLINK_SUFFIX
# TYPE one of: lib, bin, etc
AOSPEXT_GEN_TARGETS := \
    lib:libavcodec.so::libavcodec:               \
    lib:libavdevice.so::libavdevice:             \
    lib:libavfilter.so::libavfilter:             \
    lib:libavformat.so::libavformat:             \
    lib:libavutil.so::libavutil:                 \
    lib:libswresample.so::libswresample:         \
    lib:libswscale.so::libswscale:               \

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libc libdrm libc++ libudev
FFMPEG_GEN_PKGCONFIGS := libdrm:$(LIBDRM_VERSION) libudev

#-------------------------------------------------------------------------------

LOCAL_MULTILIB := first
include $(LOCAL_PATH)/ffmpeg_cross.mk
FIRSTARCH_FFMPEG_TARGET:=$(FFMPEG_GEN_FILES_TARGET)

ifdef TARGET_2ND_ARCH
LOCAL_MULTILIB := 32
include $(LOCAL_PATH)/ffmpeg_cross.mk
SECONDARCH_FFMPEG_TARGET:=$(FFMPEG_GEN_FILES_TARGET)
endif

#-------------------------------------------------------------------------------

LOCAL_MULTILIB := first
AOSPEXT_TARGETS_DEP:=$(FIRSTARCH_FFMPEG_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(FIRSTARCH_FFMPEG_TARGET))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=$(AOSPEXT_PROJECT_INSTALL_DIR)/include
include $(LOCAL_PATH)/aospext_gen_targets.mk

ifdef TARGET_2ND_ARCH
LOCAL_MULTILIB := 32
AOSPEXT_TARGETS_DEP:=$(SECONDARCH_FFMPEG_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(SECONDARCH_FFMPEG_TARGET))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=$(AOSPEXT_PROJECT_INSTALL_DIR)/include
include $(LOCAL_PATH)/aospext_gen_targets.mk
endif

#-------------------------------------------------------------------------------

endif # BOARD_BUILD_FFMPEG
