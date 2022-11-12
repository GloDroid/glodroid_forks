# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021 GlobalLogic Ukraine
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

MY_PATH := $(call my-dir)

define create-pkgconfig
echo -e "Name: $2" \
	"\nDescription: $2" \
	"\nVersion: $3" > $1/$2.pc

endef

include $(LOCAL_PATH)/aospext_get_buildflags.mk

MESON_SRC_PATH                           := $(BOARD_$(AOSPEXT_PROJECT_NAME)_SRC_DIR)
MESON_PATCHES_DIRS                       := $(BOARD_$(AOSPEXT_PROJECT_NAME)_PATCHES_DIRS)
MESON_GEN_FILES_TARGET                   := $(AOSPEXT_OUT_DIR)/.timestamp

$(MESON_GEN_FILES_TARGET): MESON_CPU_FAMILY   := $(subst arm64,aarch64,$(TARGET_$(AOSPEXT_ARCH_PREFIX)ARCH))
$(MESON_GEN_FILES_TARGET): AOSP_FLAGS_DIR_OUT := $(call relative-to-absolute,$(AOSP_FLAGS_DIR_OUT))
$(MESON_GEN_FILES_TARGET): MESON_OUT_SRC_DIR  := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/out_src)
$(MESON_GEN_FILES_TARGET): MESON_BUILD_DIR    := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/build)
$(MESON_GEN_FILES_TARGET): MESON_GEN_DIR      := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/gen)
$(MESON_GEN_FILES_TARGET): MESON_INSTALL_DIR  := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/install)

$(MESON_GEN_FILES_TARGET): MY_PATH:=$(MY_PATH)
$(MESON_GEN_FILES_TARGET): MESON_SRC_PATH:=$(MESON_SRC_PATH)
$(MESON_GEN_FILES_TARGET): LIBDIR:=lib$(if $(TARGET_IS_64_BIT),$(if $(filter 64 first,$(LOCAL_MULTILIB)),64))

$(MESON_GEN_FILES_TARGET): MESON_GEN_PKGCONFIGS:=$(MESON_GEN_PKGCONFIGS)
$(MESON_GEN_FILES_TARGET): MESON_BUILD_ARGUMENTS:=--prefix /vendor --libdir $(LIBDIR) --datadir etc/shared --libexecdir bin \
                               --sbindir bin --localstatedir=/mnt/var --buildtype=debug $(MESON_BUILD_ARGUMENTS)

$(MESON_GEN_FILES_TARGET): MY_OUT_ABS_PATH:=$(if $(patsubst /%,,$(OUT_DIR)),$(AOSP_ABSOLUTE_PATH)/$(OUT_DIR),$(OUT_DIR))
$(MESON_GEN_FILES_TARGET): MY_ABS_PATH:=$(AOSP_ABSOLUTE_PATH)/$(MY_PATH)
$(MESON_GEN_FILES_TARGET): AR_TOOL:=$($($(AOSPEXT_ARCH_PREFIX))TARGET_AR)

AOSPEXT_TOOLS := $(sort $(shell find -L $(MY_PATH)/tools -not -path '*/\.*'))
MESON_SRCS := $(sort $(shell find -L $(MESON_SRC_PATH) -not -path '*/\.*'))
MESON_PATCHES := $(if $(MESON_PATCHES_DIRS),$(sort $(shell find -L $(MESON_PATCHES_DIRS) -not -path '*/\.*')))

$(MESON_GEN_FILES_TARGET): $(MESON_SRCS) $(MESON_PATCHES) $(AOSPEXT_TOOLS)
$(MESON_GEN_FILES_TARGET): MESON_PATCHES_DIRS:=$(MESON_PATCHES_DIRS)
$(MESON_GEN_FILES_TARGET): $(AOSP_FLAGS_DIR_OUT)/.exec.timestamp
$(MESON_GEN_FILES_TARGET): $(AOSP_FLAGS_DIR_OUT)/.sharedlib.timestamp
	# Cleanup directories. Incremental build isn't supported.
	rm -rf $(MESON_GEN_DIR)
	mkdir -p $(MESON_GEN_DIR)

	cp $(MY_ABS_PATH)/tools/aospext_cc $(dir $(MESON_GEN_DIR))/aospext_cc
	cp $(MY_ABS_PATH)/tools/gen_aospless_dir.py $(dir $(MESON_GEN_DIR))/gen_aospless_dir.py

	cp $(MY_ABS_PATH)/tools/makefile_base.mk $(dir $(MESON_GEN_DIR))/Makefile
	cp $(MY_ABS_PATH)/tools/makefile_meson.mk $(dir $(MESON_GEN_DIR))/project_specific.mk
	sed -i \
		-e 's#\[PLACE_FOR_LLVM_DIR\]#$(dir $(AR_TOOL))#g' \
		-e 's#\[PLACE_FOR_AOSP_ROOT\]#$(AOSP_ABSOLUTE_PATH)#g' \
		-e 's#\[PLACE_FOR_AOSP_OUT_DIR\]#$(MY_OUT_ABS_PATH)#g' \
		-e 's#\[PLACE_FOR_SRC_DIR\]#$(MESON_SRC_PATH)#g' \
		-e 's#\[PLACE_FOR_PATCHES_DIRS\]#$(MESON_PATCHES_DIRS)#g' \
		-e 's#\[PLACE_FOR_OUT_BASE_DIR\]#$(dir $(MESON_GEN_DIR))#g' \
		$(dir $(MESON_GEN_DIR))/Makefile

	sed -i \
		-e 's#\[PLACE_FOR_MESON_DEFS\]#$(MESON_BUILD_ARGUMENTS)#g' \
		$(dir $(MESON_GEN_DIR))/project_specific.mk

	# Prepare meson cross-compilation configuration file
	cp $(MY_ABS_PATH)/tools/meson_aosp_cross.cfg $(MESON_GEN_DIR)/aosp_cross
	sed -i \
		-e 's#$$(AR_TOOL)#$(AR_TOOL)#g' \
		-e 's#$$(MY_ABS_PATH)#$(MY_ABS_PATH)#g' \
		-e 's#$$(MESON_CPU_FAMILY)#$(MESON_CPU_FAMILY)#g' \
		$(MESON_GEN_DIR)/aosp_cross

	# Prepare package info files
	$(foreach pkg, $(MESON_GEN_PKGCONFIGS), $(call create-pkgconfig,$(MESON_GEN_DIR),$(word 1, $(subst :, ,$(pkg))),$(word 2, $(subst :, ,$(pkg)))))
ifneq ($(MESON_GEN_LLVM_STUB),)
	# Some magic for mesa3d project
	mkdir -p $(MESON_OUT_SRC_DIR)/subprojects/llvm/
	echo -e "project('llvm', 'cpp', version : '$(MESON_LLVM_VERSION)')\n" \
		"dep_llvm = declare_dependency()\n"                           \
		"has_rtti = false\n" > $(dir $@)/subprojects/llvm/meson.build
endif
	# Build meson project
	PATH=/usr/bin:/bin:/sbin:$$PATH make -C $(dir $(MESON_GEN_DIR)) cleanup all

	touch $@
