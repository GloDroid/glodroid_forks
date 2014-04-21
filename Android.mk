#
# Copyright (C) 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# libcxx isn't working on mips yet
ifneq ($(TARGET_ARCH),$(filter $(TARGET_ARCH), mips mips64))
LOCAL_PATH := $(call my-dir)

LIBCXX_SRC_FILES := \
	src/algorithm.cpp \
	src/bind.cpp \
	src/chrono.cpp \
	src/condition_variable.cpp \
	src/debug.cpp \
	src/exception.cpp \
	src/future.cpp \
	src/hash.cpp \
	src/ios.cpp \
	src/iostream.cpp \
	src/locale.cpp \
	src/memory.cpp \
	src/mutex.cpp \
	src/new.cpp \
	src/optional.cpp \
	src/random.cpp \
	src/regex.cpp \
	src/shared_mutex.cpp \
	src/stdexcept.cpp \
	src/string.cpp \
	src/strstream.cpp \
	src/system_error.cpp \
	src/thread.cpp \
	src/typeinfo.cpp \
	src/utility.cpp \
	src/valarray.cpp \
	src/stubs.cpp \

LIBCXX_CPPFLAGS := \
	-I$(LOCAL_PATH)/include/ \
	-Iexternal/libcxxabi/include \
	-std=c++11 \
	-nostdlib \
	-fexceptions \

include $(CLEAR_VARS)
LOCAL_MODULE := libc++
LOCAL_CLANG := true
LOCAL_SRC_FILES := $(LIBCXX_SRC_FILES)
LOCAL_CPPFLAGS := $(LIBCXX_CPPFLAGS)
LOCAL_SYSTEM_SHARED_LIBRARIES := libc
LOCAL_SHARED_LIBRARIES := libcxxabi

ifneq ($(TARGET_ARCH),arm)
	LOCAL_SHARED_LIBRARIES += libdl
endif

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libc++
LOCAL_CLANG := true
LOCAL_SRC_FILES := $(LIBCXX_SRC_FILES)
LOCAL_CPPFLAGS := $(LIBCXX_CPPFLAGS)

ifeq ($(HOST_OS), darwin)
LOCAL_LDFLAGS := \
            -Wl,-unexported_symbols_list,external/libcxx/lib/libc++unexp.exp  \
            -Wl,-reexported_symbols_list,external/libcxx/lib/libc++abi2.exp \
            -Wl,-force_symbols_not_weak_list,external/libcxx/lib/notweak.exp \
            -Wl,-force_symbols_weak_list,external/libcxx/lib/weak.exp
else
LOCAL_LDFLAGS :=  -lrt -lpthread
endif

LOCAL_SHARED_LIBRARIES := libcxxabi
include $(BUILD_HOST_SHARED_LIBRARY)
endif
