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

# To use libc++, "include external/libcxx/libcxx.mk" in your target.

# We put the STL libraries in front of any user libraries, but we need to
# keep the RTTI stuff in abi/cpp/include in front of our STL headers.
LOCAL_C_INCLUDES := \
	$(filter abi/cpp/include,$(LOCAL_C_INCLUDES)) \
	external/libcxx/include \
	bionic \
	bionic/libstdc++/include \
	$(filter-out abi/cpp/include,$(LOCAL_C_INCLUDES))

LOCAL_SHARED_LIBRARIES += libc++
