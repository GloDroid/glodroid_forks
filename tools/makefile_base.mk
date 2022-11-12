# Variables are replaced with real data by top-level make:

AOSP_ROOT:=$(shell pwd)
AOSP_OUT_DIR:=$(shell pwd)
SRC_DIR:=./src
PATCHES_DIRS:=patches
LLVM_DIR:=

ifeq ($(AOSPLESS),)
AOSP_ROOT:=[PLACE_FOR_AOSP_ROOT]
AOSP_OUT_DIR:=[PLACE_FOR_AOSP_OUT_DIR]
SRC_DIR:=$(AOSP_ROOT)/[PLACE_FOR_SRC_DIR]
PATCHES_DIRS:=[PLACE_FOR_PATCHES_DIRS]
LLVM_DIR:=$(AOSP_ROOT)/[PLACE_FOR_LLVM_DIR]
endif


OUT_BASE_DIR:=$(shell pwd)
OUT_SRC_DIR:=$(OUT_BASE_DIR)/out_src
OUT_INSTALL_DIR:=$(OUT_BASE_DIR)/install
OUT_BUILD_DIR:=$(OUT_BASE_DIR)/build
OUT_GEN_DIR:=$(OUT_BASE_DIR)/gen

PATCHES:=$(foreach dir,$(PATCHES_DIRS), $(sort $(shell find -L $(AOSP_ROOT)/$(dir)/* -not -path '*/\.*')))

.PHONY: help cleanup all copy patch configure build install src_patch src_unpatch src_gen_pathes
.DEFAULT_GOAL = help

help: ## Show this help
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@exit || exit # Protection against running this makefile as script (otherwise it will invoke 'rm -rf /*' eventually)

cleanup: ## Cleanup
	@[ "$(OUT_SRC_DIR)" ] || ( echo "var is not set"; exit 1 )
	@[ "$(OUT_INSTALL_DIR)" ] || ( echo "var is not set"; exit 1 )
	@[ "$(OUT_BUILD_DIR)" ] || ( echo "var is not set"; exit 1 )
	rm -rf $(OUT_SRC_DIR)/*
	rm -rf $(OUT_INSTALL_DIR)/*
	rm -rf $(OUT_BUILD_DIR)/*

src_patch: ## Patch sources in sources dir using git am (not implemented)
src_unpatch: ## Reset source git (not implemented)
src_gen_patches: ## Generate patches (not implemented)

all: copy patch configure build install
all: ## Invoke copy,patch,configure,build,install actions

copy: ## Copy sources into intermediate directory
	@echo Copying...
	mkdir -p $(OUT_SRC_DIR)
	rsync -ar $(SRC_DIR)/* $(OUT_SRC_DIR)

patch: ## Patch sources in intermediate directory
	@echo Patching...
	@cd $(OUT_SRC_DIR) && $(foreach patch,$(PATCHES), echo "\nApplying $(notdir $(patch))" && patch -f -p1 < $(patch) &&) true

include project_specific.mk
