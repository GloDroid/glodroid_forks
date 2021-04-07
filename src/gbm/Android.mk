#
# Copyright (C) 2016 Linaro, Ltd., Rob Herring <robh@kernel.org>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/main
LOCAL_C_INCLUDES := $(LOCAL_PATH)/main

LOCAL_SHARED_LIBRARIES := libdrm
LOCAL_STATIC_LIBRARIES := libmesa_loader libmesa_util
LOCAL_HEADER_LIBRARIES := mesa3d_headers
LOCAL_MODULE := libgbm

LOCAL_CFLAGS := \
    -DHAVE_PTHREAD=1 \
    -Wno-unused-parameter \
    -Wno-missing-field-initializers \

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29 && echo true),true)
LOCAL_CFLAGS += -DHAVE_TIMESPEC_GET
endif

LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := \
	main/backend.c \
	main/gbm.c \
	backends/dri/gbm_dri.c \

include $(BUILD_SHARED_LIBRARY)
