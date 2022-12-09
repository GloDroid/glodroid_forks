# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/meson_dbus.mk
include $(LOCAL_PATH)/meson_glib.mk
include $(LOCAL_PATH)/meson_libgudev.mk
include $(LOCAL_PATH)/meson_mesa3d.mk
include $(LOCAL_PATH)/meson_libcamera.mk
#include $(LOCAL_PATH)/meson_libmbim.mk
#include $(LOCAL_PATH)/meson_libqrtr.mk
include $(LOCAL_PATH)/meson_libqmi.mk
include $(LOCAL_PATH)/meson_modemmanager.mk
include $(LOCAL_PATH)/meson_mmradio.mk
include $(LOCAL_PATH)/meson_drmhwcomposer.mk
include $(LOCAL_PATH)/ffmpeg.mk
