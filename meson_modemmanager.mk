# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

AOSPEXT_PROJECT_NAME := MODEMMANAGER

ifneq ($(filter true, $(BOARD_BUILD_AOSPEXT_MODEMMANAGER)),)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libc libexpat libglib-2.0 libgio-2.0 libgobject-2.0 libgmodule-2.0 libdbus-1 libqmi-glib libgudev-1.0
MESON_GEN_PKGCONFIGS := glib-2.0:2.75.1 gmodule-2.0:2.75.1 gobject-2.0:2.75.1 gio-2.0:2.75.1 gio-unix-2.0:2.75.1 dbus-1 qmi-glib:1.33.2 gudev-1.0:232

MESON_BUILD_ARGUMENTS := \
    -Dmbim=false \
    -Dqrtr=false \
    -Dtests=false \
    -Dintrospection=false \
    -Dbash_completion=false \
    -Dsystemdsystemunitdir=no \
    -Dsystemd_suspend_resume=false \
    -Dsystemd_journal=false \
    -Dpolkit=no \
    -Dplugin_dell=disabled \
    -Dplugin_foxconn=disabled \
    -Dudevdir=/vendor/etc/mm_udev \

TMP_MODEMMANAGER_DRIVERS := \
    libmm-plugin-altair-lte    libmm-plugin-fibocom  libmm-plugin-linktop      libmm-plugin-novatel-lte  libmm-plugin-samsung        libmm-plugin-tplink   libmm-shared-icera \
    libmm-plugin-anydata       libmm-plugin-generic  libmm-plugin-longcheer    libmm-plugin-novatel      libmm-plugin-sierra-legacy  libmm-plugin-ublox    libmm-shared-novatel \
    libmm-plugin-broadmobi     libmm-plugin-gosuncn  libmm-plugin-motorola     libmm-plugin-option-hso   libmm-plugin-sierra         libmm-plugin-via      libmm-shared-option \
    libmm-plugin-cinterion     libmm-plugin-haier    libmm-plugin-mtk          libmm-plugin-option       libmm-plugin-simtech        libmm-plugin-wavecom  libmm-shared-sierra \
    libmm-plugin-dlink         libmm-plugin-huawei   libmm-plugin-nokia-icera  libmm-plugin-pantech      libmm-plugin-telit          libmm-plugin-x22x     libmm-shared-telit \
    libmm-plugin-ericsson-mbm  libmm-plugin-iridium  libmm-plugin-nokia        libmm-plugin-quectel      libmm-plugin-thuraya        libmm-plugin-zte      libmm-shared-xmm \
    libmm-plugin-qcom-soc

# Format: TYPE:REL_PATH_TO_INSTALL_ARTIFACT:VENDOR_SUBDIR:MODULE_NAME:SYMLINK_SUFFIX
# TYPE one of: lib, bin, etc
AOSPEXT_GEN_TARGETS := \
    bin:mmcli::mmcli:               \
    bin:ModemManager::ModemManager: \
    lib:libmm-int.so::libmm-int:    \
    lib:libmm-glib.so::libmm-glib:  \
    etc:dbus-1/system.d/org.freedesktop.ModemManager1.conf:dbus-1/system.d:org.freedesktop.ModemManager1.conf: \
    $(BOARD_$(AOSPEXT_PROJECT_NAME)_EXTRA_TARGETS)

AOSPEXT_GEN_TARGETS += \
    $(foreach lib,$(TMP_MODEMMANAGER_DRIVERS), lib:ModemManager/$(lib).so:ModemManager:$(lib):)

LOCAL_MULTILIB := first
include $(LOCAL_PATH)/meson_cross.mk
AOSPEXT_TARGETS_DEP:=$(MESON_GEN_FILES_TARGET)
AOSPEXT_PROJECT_INSTALL_DIR:=$(dir $(AOSPEXT_TARGETS_DEP))/install
AOSPEXT_PROJECT_OUT_INCLUDE_DIR:=$(AOSPEXT_PROJECT_INSTALL_DIR)/vendor/include/libmm-glib $(AOSPEXT_PROJECT_INSTALL_DIR)/vendor/include/ModemManager
include $(LOCAL_PATH)/aospext_gen_targets.mk

#-------------------------------------------------------------------------------

endif # BOARD_BUILD_AOSPEXT_MODEMMANAGER
