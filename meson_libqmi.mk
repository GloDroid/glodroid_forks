# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

AOSPEXT_PROJECT_NAME := LIBQMI

ifneq ($(filter true, $(BOARD_BUILD_AOSPEXT_LIBQMI)),)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libc libglib-2.0 libgio-2.0 libgobject-2.0
MESON_GEN_PKGCONFIGS := glib-2.0:2.75.1 gio-2.0:2.75.1 gio-unix-2.0:2.75.1 gobject-2.0:2.75.1

MESON_BUILD_ARGUMENTS := \
    -Dudev=false \
    -Dbash_completion=false \
    -Dintrospection=false \
    -Dmbim_qmux=false \
    -Dqrtr=false \
    -Dman=false \

TMP_OUT_BIN := qmicli qmi-firmware-update qmi-network qmi-proxy

# Format: TYPE:REL_PATH_TO_INSTALL_ARTIFACT:VENDOR_SUBDIR:MODULE_NAME:SYMLINK_SUFFIX
# TYPE one of: lib, bin, etc
AOSPEXT_GEN_TARGETS := \
    lib:libqmi-glib.so::libqmi-glib: \
    $(BOARD_LIBQMI_EXTRA_TARGETS)

AOSPEXT_GEN_TARGETS += \
    $(foreach bin,$(TMP_OUT_BIN), bin:$(bin)::$(bin):)

# Build first ARCH only
LOCAL_MULTILIB := first
include $(LOCAL_PATH)/meson_cross.mk
AOSPEXT_TARGETS_DEP:=$(MESON_GEN_FILES_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(AOSPEXT_TARGETS_DEP))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=$(AOSPEXT_PROJECT_INSTALL_DIR)/vendor/include/libqmi-glib
include $(LOCAL_PATH)/aospext_gen_targets.mk

#-------------------------------------------------------------------------------

endif # BOARD_BUILD_AOSPEXT_LIBQMI
