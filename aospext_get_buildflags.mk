# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

# Inputs:
#  LOCAL_MULTILIB - (first or 32)
#  AOSPEXT_PROJECT_NAME - (i.e. mesa3d / libcamera / ffmpeg)
#  Kati LOCAL_* definitions to define dependencies / extra flags
# Outputs:
#  AOSPEXT_OUT_DIR
#  AOSPEXT_ARCH_PREFIX is set
#  SHLIB_AOSP_FLAGS_DIR_OUT
#  EXEC_AOSP_FLAGS_DIR_OUT
#  Files with flag will be located in the AOSP_FLAGS_DIR_OUT directory

MY_PATH := $(call my-dir)

AOSP_ABSOLUTE_PATH := $(realpath .)
define relative-to-absolute
$(if $(patsubst /%,,$1),$(AOSP_ABSOLUTE_PATH)/$1,$1)
endef

LOCAL_PATH := $(MY_PATH)

# Build mesa3d using intermediate variables provided by AOSP make/core internalsn
define extract_build_deps

TMP_AOSPEXT_SO_DEPS := \
	$(built_static_libraries) \
	$(built_shared_libraries) \
	$(built_whole_libraries) \
	$(strip $(all_objects)) \
	$(my_target_libatomic) \
	$(my_target_libcrt_builtins) \
	$(my_target_crtbegin_so_o) \
	$(my_target_crtend_so_o)

TMP_AOSPEXT_EXEC_DEPS := \
	$(built_static_libraries) \
	$(built_shared_libraries) \
	$(built_whole_libraries) \
	$(strip $(all_objects)) \
	$(my_target_libatomic) \
	$(my_target_libcrt_builtins) \
	$(my_target_crtbegin_dynamic_o) \
	$(my_target_crtend_o)

# Taken from build/make/core/binary.mk. We need this
# to use definitions from build/make/core/definitions.mk
$(TMP_AOSPEXT_TARGET): PRIVATE_GLOBAL_C_INCLUDES := $(my_target_global_c_includes)
$(TMP_AOSPEXT_TARGET): PRIVATE_GLOBAL_C_SYSTEM_INCLUDES := $(my_target_global_c_system_includes)

$(TMP_AOSPEXT_TARGET): PRIVATE_2ND_ARCH_VAR_PREFIX := $(AOSPEXT_ARCH_PREFIX)
$(TMP_AOSPEXT_TARGET): PRIVATE_CC := $(my_cc)
$(TMP_AOSPEXT_TARGET): PRIVATE_LINKER := $(my_linker)
$(TMP_AOSPEXT_TARGET): PRIVATE_CXX := $(my_cxx)
$(TMP_AOSPEXT_TARGET): PRIVATE_CXX_LINK := $(my_cxx_link)
$(TMP_AOSPEXT_TARGET): PRIVATE_YACCFLAGS := $(LOCAL_YACCFLAGS)
$(TMP_AOSPEXT_TARGET): PRIVATE_ASFLAGS := $(my_asflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_CONLYFLAGS := $(my_conlyflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_CFLAGS := $(my_cflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_CPPFLAGS := $(my_cppflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_CFLAGS_NO_OVERRIDE := $(my_cflags_no_override)
$(TMP_AOSPEXT_TARGET): PRIVATE_CPPFLAGS_NO_OVERRIDE := $(my_cppflags_no_override)
$(TMP_AOSPEXT_TARGET): PRIVATE_RTTI_FLAG := $(LOCAL_RTTI_FLAG)
$(TMP_AOSPEXT_TARGET): PRIVATE_DEBUG_CFLAGS := $(debug_cflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_C_INCLUDES := $(my_c_includes)
$(TMP_AOSPEXT_TARGET): PRIVATE_IMPORTED_INCLUDES := $(imported_includes)
$(TMP_AOSPEXT_TARGET): PRIVATE_LDFLAGS := $(my_ldflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_LDLIBS := $(my_ldlibs)
$(TMP_AOSPEXT_TARGET): PRIVATE_TIDY_CHECKS := $(my_tidy_checks)
$(TMP_AOSPEXT_TARGET): PRIVATE_TIDY_FLAGS := $(my_tidy_flags)
$(TMP_AOSPEXT_TARGET): PRIVATE_ARFLAGS := $(my_arflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_ALL_SHARED_LIBRARIES := $(built_shared_libraries)
$(TMP_AOSPEXT_TARGET): PRIVATE_ALL_STATIC_LIBRARIES := $(built_static_libraries)
$(TMP_AOSPEXT_TARGET): PRIVATE_ALL_WHOLE_STATIC_LIBRARIES := $(built_whole_libraries)
$(TMP_AOSPEXT_TARGET): PRIVATE_ALL_OBJECTS := $(strip $(all_objects))

$(TMP_AOSPEXT_TARGET): PRIVATE_ARM_CFLAGS := $(normal_objects_cflags)

$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_GLOBAL_CFLAGS := $(my_target_global_cflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_GLOBAL_CONLYFLAGS := $(my_target_global_conlyflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_GLOBAL_CPPFLAGS := $(my_target_global_cppflags)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_GLOBAL_LDFLAGS := $(my_target_global_ldflags)

$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_LIBCRT_BUILTINS := $(my_target_libcrt_builtins)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_LIBATOMIC := $(my_target_libatomic)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_CRTBEGIN_SO_O := $(my_target_crtbegin_so_o)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_CRTEND_SO_O := $(my_target_crtend_so_o)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_CRTBEGIN_DYNAMIC_O := $(my_target_crtbegin_dynamic_o)
$(TMP_AOSPEXT_TARGET): PRIVATE_TARGET_CRTEND_O := $(my_target_crtend_o)
##

endef

define m-lld-so-flags
  -nostdlib -Wl,--gc-sections \
  $(PRIVATE_TARGET_CRTBEGIN_SO_O) \
  $(PRIVATE_ALL_OBJECTS) \
  -Wl,--whole-archive \
  $(PRIVATE_ALL_WHOLE_STATIC_LIBRARIES) \
  -Wl,--no-whole-archive \
  $(if $(PRIVATE_GROUP_STATIC_LIBRARIES),-Wl$(comma)--start-group) \
  $(PRIVATE_ALL_STATIC_LIBRARIES) \
  $(if $(PRIVATE_GROUP_STATIC_LIBRARIES),-Wl$(comma)--end-group) \
  $(if $(filter true,$(NATIVE_COVERAGE)),$(PRIVATE_TARGET_COVERAGE_LIB)) \
  $(PRIVATE_TARGET_LIBCRT_BUILTINS) \
  $(PRIVATE_TARGET_LIBATOMIC) \
  $(PRIVATE_TARGET_GLOBAL_LDFLAGS) \
  $(PRIVATE_LDFLAGS) \
  $(PRIVATE_ALL_SHARED_LIBRARIES) \
  $(PRIVATE_TARGET_CRTEND_SO_O) \
  $(PRIVATE_LDLIBS)
endef

define m-lld-exec-flags
  -nostdlib -Wl,--gc-sections \
  $(PRIVATE_TARGET_CRTBEGIN_DYNAMIC_O) \
  $(PRIVATE_ALL_OBJECTS) \
  -Wl,--whole-archive \
  $(PRIVATE_ALL_WHOLE_STATIC_LIBRARIES) \
  -Wl,--no-whole-archive \
  $(if $(PRIVATE_GROUP_STATIC_LIBRARIES),-Wl$(comma)--start-group) \
  $(PRIVATE_ALL_STATIC_LIBRARIES) \
  $(if $(PRIVATE_GROUP_STATIC_LIBRARIES),-Wl$(comma)--end-group) \
  $(if $(filter true,$(NATIVE_COVERAGE)),$(PRIVATE_TARGET_COVERAGE_LIB)) \
  $(PRIVATE_TARGET_LIBCRT_BUILTINS) \
  $(PRIVATE_TARGET_LIBATOMIC) \
  $(PRIVATE_TARGET_GLOBAL_LDFLAGS) \
  $(PRIVATE_LDFLAGS) \
  $(PRIVATE_ALL_SHARED_LIBRARIES) \
  $(PRIVATE_TARGET_CRTEND_O) \
  $(PRIVATE_LDLIBS)
endef

define m-process-lld-flags
  $(patsubst -Wl$(comma)--build-id=%,,                             \
   $(subst prebuilts/,$(AOSP_ABSOLUTE_PATH)/prebuilts/,            \
    $(subst $(OUT_DIR)/,$(call relative-to-absolute,$(OUT_DIR))/,  \
     $(patsubst %dummy.o,,                                         \
       $(filter-out -pie                                           \
                    -Wl$(comma)--gc-sections                       \
                    -Wl$(comma)--no-undefined-version              \
                    -Wl$(comma)--fatal-warnings                    \
                    -Wl$(comma)--no-undefined                      \
         ,$1)                                                      \
     ) \
    )  \
   )   \
  )    \

endef

define m-cpp-flags
  $(PRIVATE_TARGET_GLOBAL_CFLAGS) \
  $(PRIVATE_TARGET_GLOBAL_CPPFLAGS) \
  $(PRIVATE_ARM_CFLAGS) \
  $(PRIVATE_RTTI_FLAG) \
  $(PRIVATE_CFLAGS) \
  $(PRIVATE_CPPFLAGS) \
  $(PRIVATE_DEBUG_CFLAGS) \
  $(PRIVATE_CFLAGS_NO_OVERRIDE) \
  $(PRIVATE_CPPFLAGS_NO_OVERRIDE)
endef

define m-c-flags
  $(PRIVATE_TARGET_GLOBAL_CFLAGS) \
  $(PRIVATE_TARGET_GLOBAL_CONLYFLAGS) \
  $(PRIVATE_ARM_CFLAGS) \
  $(PRIVATE_CFLAGS) \
  $(PRIVATE_CONLYFLAGS) \
  $(PRIVATE_DEBUG_CFLAGS) \
  $(PRIVATE_CFLAGS_NO_OVERRIDE)
endef

define filter-c-flags
  $(filter-out -fno-rtti,
    $(patsubst -std=%,, \
      $(patsubst -f%,, \
        $(patsubst  -W%,, $1))))
endef

define nospace-includes
  $(subst $(space)-isystem$(space),$(space)-isystem, \
  $(subst $(space)-I$(space),$(space)-I, \
  $(strip $(c-includes))))
endef

# Ensure include paths are always absolute
# When OUT_DIR_COMMON_BASE env variable is set the AOSP/KATI will use absolute paths
# for headers in intermediate output directories, but relative for all others.
define abs-include
$(strip \
  $(if $(patsubst -I%,,$1),\
    $(if $(patsubst -isystem/%,,$1),\
      $(subst -isystem,-isystem$(AOSP_ABSOLUTE_PATH)/,$1),\
      $1\
    ),\
    $(if $(patsubst -I/%,,$1),\
      $(subst -I,-I$(AOSP_ABSOLUTE_PATH)/,$1),\
      $1\
    )\
  )
)
endef

LOCAL_VENDOR_MODULE := true

# Extract flags for shared libraries

LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE := aospext.sharedlib.dummy.$(LOCAL_MULTILIB).$(AOSPEXT_PROJECT_NAME)

TMP_AOSPEXT_DUMMY := $(local-generated-sources-dir)/dummy.c
$(TMP_AOSPEXT_DUMMY):
	mkdir -p $(dir $@)
	echo "int main() {}" > $@

LOCAL_GENERATED_SOURCES := $(TMP_AOSPEXT_DUMMY)

# Prepare intermediate variables by AOSP make/core internals
include $(BUILD_SHARED_LIBRARY)

AOSPEXT_ARCH_PREFIX := $(my_2nd_arch_prefix)

LOCAL_BUILT_MODULE :=
LOCAL_INSTALLED_MODULE :=

AOSPEXT_OUT_DIR := $($(AOSPEXT_ARCH_PREFIX)TARGET_OUT_INTERMEDIATES)/AOSPEXT/$(AOSPEXT_PROJECT_NAME)
AOSP_FLAGS_DIR_OUT := $(AOSPEXT_OUT_DIR)/build_flags
TMP_AOSPEXT_TARGET := $(AOSP_FLAGS_DIR_OUT)/.sharedlib.timestamp
$(eval $(call extract_build_deps))

$(TMP_AOSPEXT_TARGET): $(TMP_AOSPEXT_SO_DEPS)
	mkdir -p $(dir $@)
	echo -n "$(foreach flag,$(call filter-c-flags,$(m-c-flags)),$(flag) )   " >  $(dir $@)/sharedlib.cflags
	echo -n "$(foreach inc,$(nospace-includes),$(call abs-include,$(inc)) ) " >> $(dir $@)/sharedlib.cflags
	echo -n "$(foreach flag,$(call filter-c-flags,$(m-cpp-flags)),$(flag) ) " >  $(dir $@)/sharedlib.cppflags
	echo -n "$(foreach inc,$(nospace-includes),$(call abs-include,$(inc)) ) " >> $(dir $@)/sharedlib.cppflags
	echo -n "$(foreach flag, $(call m-process-lld-flags,$(m-lld-so-flags)),$(flag) ) " > $(dir $@)/sharedlib.link_args
	echo -n "$(foreach exec,$(PRIVATE_CC),$(call relative-to-absolute,$(exec)) ) "     > $(dir $@)/sharedlib.cc
	echo -n "$(foreach exec,$(PRIVATE_CXX),$(call relative-to-absolute,$(exec)) ) "    > $(dir $@)/sharedlib.cxx
	touch $@

# Extract flags for executables

LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE := aospext.executable.dummy.$(LOCAL_MULTILIB).$(AOSPEXT_PROJECT_NAME)

TMP_AOSPEXT_DUMMY := $(local-generated-sources-dir)/dummy.c
$(TMP_AOSPEXT_DUMMY):
	mkdir -p $(dir $@)
	echo "int main() {}" > $@

LOCAL_GENERATED_SOURCES := $(TMP_AOSPEXT_DUMMY)

# Prepare intermediate variables by AOSP make/core internals

include $(BUILD_EXECUTABLE)
LOCAL_BUILT_MODULE :=
LOCAL_INSTALLED_MODULE :=

TMP_AOSPEXT_TARGET := $(AOSP_FLAGS_DIR_OUT)/.exec.timestamp
$(eval $(call extract_build_deps))

$(TMP_AOSPEXT_TARGET): $(TMP_AOSPEXT_EXEC_DEPS)
	mkdir -p $(dir $@)
	echo -n "$(foreach flag,$(call filter-c-flags,$(m-c-flags)),$(flag) ) "   >  $(dir $@)/exec.cflags
	echo -n "$(foreach inc,$(nospace-includes),$(call abs-include,$(inc)) ) " >> $(dir $@)/exec.cflags
	echo -n "$(foreach flag,$(call filter-c-flags,$(m-cpp-flags)),$(flag) ) " >  $(dir $@)/exec.cppflags
	echo -n "$(foreach inc,$(nospace-includes),$(call abs-include,$(inc)) ) " >> $(dir $@)/exec.cppflags
	echo -n "$(foreach flag, $(call m-process-lld-flags,$(m-lld-exec-flags)),$(flag) ) " > $(dir $@)/exec.link_args
	echo -n "$(foreach exec,$(PRIVATE_CC),$(call relative-to-absolute,$(exec)) ) "       > $(dir $@)/exec.cc
	echo -n "$(foreach exec,$(PRIVATE_CXX),$(call relative-to-absolute,$(exec)) ) "      > $(dir $@)/exec.cxx
	touch $@
