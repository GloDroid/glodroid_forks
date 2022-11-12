MESON_DEFS:=[PLACE_FOR_MESON_DEFS]

configure: export BASE_DIR = $(OUT_BASE_DIR)
configure: ## Configure the project
	@echo Configuring...
	echo "[constants]" > $(OUT_BASE_DIR)/gen/aosp_cross.out
	echo "base_dir='$(OUT_BASE_DIR)'" >> $(OUT_BASE_DIR)/gen/aosp_cross.out
	echo "llvm_dir='$(LLVM_DIR)'" >> $(OUT_BASE_DIR)/gen/aosp_cross.out
	cat $(OUT_BASE_DIR)/gen/aosp_cross >> $(OUT_BASE_DIR)/gen/aosp_cross.out

	cd $(OUT_SRC_DIR) && meson $(OUT_BUILD_DIR) --cross-file $(OUT_BASE_DIR)/gen/aosp_cross.out $(MESON_DEFS)

build: export BASE_DIR = $(OUT_BASE_DIR)
build: ## Build the project
	@echo Building...
	mkdir -p $(OUT_BUILD_DIR)
	ninja -C $(OUT_BUILD_DIR)

install: ## Install the project
	@echo Installing...
	mkdir -p $(OUT_INSTALL_DIR)
	DESTDIR=$(OUT_INSTALL_DIR) ninja -C $(OUT_BUILD_DIR) install

gen_aospless: ## Generate tree for building without AOSP or NDK
	L_AOSP_ROOT=$(AOSP_ROOT) L_AOSP_OUT_DIR=$(AOSP_OUT_DIR) python3 $(OUT_BASE_DIR)/gen_aospless_dir.py
