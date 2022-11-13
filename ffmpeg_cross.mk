# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2022 Roman Stratiienko (r.stratiienko@gmail.com)

MY_PATH := $(call my-dir)

define create-pkgconfig
echo -e "Name: $2" \
	"\nDescription: $2" \
	"\nVersion: $3" > $1/$2.pc

endef

AOSPEXT_OUT_DIR                            := $($(AOSPEXT_ARCH_PREFIX)TARGET_OUT_INTERMEDIATES)/AOSPEXT/$(AOSPEXT_PROJECT_NAME)

include $(LOCAL_PATH)/aospext_get_buildflags.mk

FFMPEG_GEN_FILES_TARGET                   := $(AOSPEXT_OUT_DIR)/.timestamp
FFMPEG_SRC_PATH                           := $(BOARD_$(AOSPEXT_PROJECT_NAME)_SRC_DIR)

$(FFMPEG_GEN_FILES_TARGET): FFMPEG_ARCH   := $(TARGET_$(AOSPEXT_ARCH_PREFIX)ARCH)
$(FFMPEG_GEN_FILES_TARGET): AOSP_FLAGS_DIR_OUT := $(call relative-to-absolute,$(AOSP_FLAGS_DIR_OUT))
$(FFMPEG_GEN_FILES_TARGET): FFMPEG_OUT_SRC_DIR  := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/out_src)
$(FFMPEG_GEN_FILES_TARGET): FFMPEG_BUILD_DIR    := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/build)
$(FFMPEG_GEN_FILES_TARGET): FFMPEG_GEN_DIR      := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/gen)
$(FFMPEG_GEN_FILES_TARGET): FFMPEG_INSTALL_DIR  := $(call relative-to-absolute,$(AOSPEXT_OUT_DIR)/install/vendor)

$(FFMPEG_GEN_FILES_TARGET): MY_PATH:=$(MY_PATH)
$(FFMPEG_GEN_FILES_TARGET): AOSP_FLAGS_DIR_OUT:=$(AOSP_FLAGS_DIR_OUT)
$(FFMPEG_GEN_FILES_TARGET): FFMPEG_SRC_PATH:=$(FFMPEG_SRC_PATH)
$(FFMPEG_GEN_FILES_TARGET): LIBDIR:=lib$(if $(TARGET_IS_64_BIT),$(if $(filter 64 first,$(LOCAL_MULTILIB)),64))

$(FFMPEG_GEN_FILES_TARGET): FFMPEG_GEN_PKGCONFIGS:=$(FFMPEG_GEN_PKGCONFIGS)
$(FFMPEG_GEN_FILES_TARGET): FFMPEG_BUILD_ARGUMENTS:=$(FFMPEG_BUILD_ARGUMENTS)

$(FFMPEG_GEN_FILES_TARGET): MY_ABS_PATH:=$(AOSP_ABSOLUTE_PATH)/$(MY_PATH)
$(FFMPEG_GEN_FILES_TARGET): AR_TOOL:=$(AOSP_ABSOLUTE_PATH)/$($($(AOSPEXT_ARCH_PREFIX))TARGET_AR)
$(FFMPEG_GEN_FILES_TARGET): NM_TOOL:=$(AOSP_ABSOLUTE_PATH)/$(dir $($($(AOSPEXT_ARCH_PREFIX))TARGET_AR))/llvm-nm
$(FFMPEG_GEN_FILES_TARGET): RANLIB_TOOL:=$(AOSP_ABSOLUTE_PATH)/$(dir $($($(AOSPEXT_ARCH_PREFIX))TARGET_AR))/llvm-ranlib

$(FFMPEG_GEN_FILES_TARGET): $(sort $(shell find -L $(FFMPEG_SRC_PATH) -not -path '*/\.*'))
$(FFMPEG_GEN_FILES_TARGET): $(MY_PATH)/aospext_cc
$(FFMPEG_GEN_FILES_TARGET): $(MY_PATH)/ffmpeg-build.sh
$(FFMPEG_GEN_FILES_TARGET): $(AOSP_FLAGS_DIR_OUT)/.exec.timestamp
$(FFMPEG_GEN_FILES_TARGET): $(AOSP_FLAGS_DIR_OUT)/.sharedlib.timestamp
	# Cleanup directories. Incremental build isn't supported.
	rm -rf $(FFMPEG_GEN_DIR)
	rm -rf $(FFMPEG_OUT_SRC_DIR)
	rm -rf $(FFMPEG_INSTALL_DIR)
	mkdir -p $(FFMPEG_GEN_DIR)
	mkdir -p $(FFMPEG_OUT_SRC_DIR)

	# Copy meson sources
	# Meson will update timestamps in sources directory, continuously retriggering the build
	# even if nothing changed. Copy sources into intermediate dir to avoid this effect.
	cp -r $(FFMPEG_SRC_PATH)/* $(FFMPEG_OUT_SRC_DIR)

	# Prepare meson cross-compilation configuration file
	cp $(MY_ABS_PATH)/ffmpeg-build.sh $(FFMPEG_GEN_DIR)/ffmpeg-build.sh
	sed -i \
		-e 's#$$(LIBDIR)#$(FFMPEG_INSTALL_DIR)/$(LIBDIR)#g' \
		-e 's#$$(AR_TOOL)#$(AR_TOOL)#g' \
		-e 's#$$(NM_TOOL)#$(NM_TOOL)#g' \
		-e 's#$$(RANLIB_TOOL)#$(RANLIB_TOOL)#g' \
		-e 's#$$(MY_ABS_PATH)#$(MY_ABS_PATH)#g' \
		-e 's#$$(AOSP_FLAGS_DIR_OUT)#$(AOSP_FLAGS_DIR_OUT)#g' \
		-e 's#$$(FFMPEG_GEN_DIR)#$(FFMPEG_GEN_DIR)#g' \
		-e 's#$$(FFMPEG_INSTALL_DIR)#$(FFMPEG_INSTALL_DIR)#g' \
		-e 's#$$(FFMPEG_ARCH)#$(FFMPEG_ARCH)#g' \
		$(FFMPEG_GEN_DIR)/ffmpeg-build.sh

	# Prepare package info files
	$(foreach pkg, $(FFMPEG_GEN_PKGCONFIGS), $(call create-pkgconfig,$(FFMPEG_GEN_DIR),$(word 1, $(subst :, ,$(pkg))),$(word 2, $(subst :, ,$(pkg)))))
	# Configure, build and install ffmpeg
	cd $(FFMPEG_OUT_SRC_DIR) && PATH=/usr/bin:/bin:/sbin:$$PATH /bin/bash $(FFMPEG_GEN_DIR)/ffmpeg-build.sh
	touch $@
