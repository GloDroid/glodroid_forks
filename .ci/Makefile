
INCLUDE_DIRS := . ../libdrm/include/drm include ./.ci/android_headers ./tests/test_include
SYSTEM_INCLUDE_DIRS := /usr/include/libdrm

CLANG := clang++-12
CLANG_TIDY := clang-tidy-12
OUT_DIR := /tmp/drm_hwcomposer/build/
SRC_DIR := .

CXXFLAGS := -fPIC -Wall -Wextra -Werror -DPLATFORM_SDK_VERSION=31 -D__ANDROID_API__=31
CXXFLAGS += -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS
CXXFLAGS += -fvisibility-inlines-hidden -std=gnu++17 -DHWC2_USE_CPP11 -DHWC2_INCLUDE_STRINGIFICATION -fno-rtti

BUILD_FILES := \
    backend/BackendClient.cpp \
    backend/Backend.cpp \
    backend/BackendManager.cpp \
    backend/BackendRCarDu.cpp \
    bufferinfo/BufferInfoGetter.cpp \
    bufferinfo/legacy/BufferInfoImagination.cpp \
    bufferinfo/legacy/BufferInfoLibdrm.cpp \
    bufferinfo/legacy/BufferInfoMaliHisi.cpp \
    bufferinfo/legacy/BufferInfoMaliMediatek.cpp \
    bufferinfo/legacy/BufferInfoMaliMeson.cpp \
    bufferinfo/legacy/BufferInfoMinigbm.cpp \
    compositor/DrmDisplayComposition.cpp \
    compositor/DrmDisplayCompositor.cpp \
    compositor/Planner.cpp \
    drm/DrmConnector.cpp \
    drm/DrmCrtc.cpp \
    drm/DrmDevice.cpp \
    drm/DrmEncoder.cpp \
    drm/DrmFbImporter.cpp \
    drm/DrmMode.cpp \
    drm/DrmPlane.cpp \
    drm/DrmProperty.cpp \
    DrmHwcTwo.cpp \
    drm/ResourceManager.cpp \
    drm/UEventListener.cpp \
    drm/VSyncWorker.cpp \
    tests/worker_test.cpp \
    utils/autolock.cpp \
    utils/Worker.cpp \

#bufferinfo/BufferInfoMapperMetadata.cpp
#utils/hwcutils.cpp

TIDY_CHECKS_NORMAL := * \
    -hicpp-* -llvmlibc-* -fuchsia-* -altera-* \
    -llvm-header-guard \
    -cppcoreguidelines-special-member-functions \
    -cppcoreguidelines-pro-type-cstyle-cast \
    -cppcoreguidelines-macro-usage \
    -cppcoreguidelines-avoid-c-arrays \
    -google-readability-braces-around-statements \
    -google-readability-casting \
    -misc-non-private-member-variables-in-classes \
    -modernize-avoid-c-arrays \
    -modernize-use-trailing-return-type \
    -readability-braces-around-statements \
    -readability-convert-member-functions-to-static \
    -readability-implicit-bool-conversion \
    -readability-identifier-naming \

TIDY_CHECKS_COARSE := \
    $(TIDY_CHECKS_NORMAL) \
    -cppcoreguidelines-pro-bounds-array-to-pointer-decay \
    -cppcoreguidelines-pro-bounds-constant-array-index \
    -cppcoreguidelines-pro-bounds-pointer-arithmetic \
    -cppcoreguidelines-pro-type-cstyle-cast \
    -cppcoreguidelines-pro-type-union-access \
    -cppcoreguidelines-pro-type-vararg \
    -cppcoreguidelines-avoid-magic-numbers \
    -cppcoreguidelines-macro-usage \
    -cppcoreguidelines-avoid-c-arrays \
    -google-readability-braces-around-statements \
    -google-readability-casting \
    -misc-non-private-member-variables-in-classes \
    -modernize-avoid-c-arrays \
    -modernize-use-trailing-return-type \
    -readability-braces-around-statements \
    -readability-convert-member-functions-to-static \
    -readability-implicit-bool-conversion \
    -readability-identifier-naming \
    -readability-magic-numbers \
    -readability-use-anyofallof \

TIDY_FILES := \
    drm/UEventListener.h:COARSE                        \
    drm/DrmFbImporter.h:FINE                           \
    drm/ResourceManager.h:                             \
    drm/DrmMode.h:                                     \
    drm/DrmDevice.h:                                   \
    drm/DrmProperty.h:                                 \
    drm/DrmConnector.h:                                \
    drm/DrmCrtc.h:                                     \
    drm/VSyncWorker.h:                                 \
    drm/DrmPlane.h:                                    \
    drm/DrmUnique.h:FINE                               \
    drm/DrmEncoder.h:                                  \
    bufferinfo/legacy/BufferInfoMinigbm.h:             \
    bufferinfo/legacy/BufferInfoLibdrm.h:              \
    bufferinfo/legacy/BufferInfoMaliMediatek.h:        \
    bufferinfo/legacy/BufferInfoMaliHisi.h:            \
    bufferinfo/legacy/BufferInfoImagination.h:         \
    bufferinfo/legacy/BufferInfoMaliMeson.h:           \
    bufferinfo/BufferInfoGetter.h:                     \
    bufferinfo/BufferInfoMapperMetadata.h:             \
    include/drmhwcgralloc.h:                           \
    include/drmhwcomposer.h:                           \
    utils/Worker.h:                                    \
    utils/UniqueFd.h:FINE                              \
    utils/autolock.h:                                  \
    utils/log.h:FINE                                   \
    utils/properties.h:FINE                            \
    compositor/DrmDisplayCompositor.h:                 \
    compositor/Planner.h:                              \
    compositor/DrmDisplayComposition.h:                \
    backend/BackendManager.h:                          \
    backend/BackendClient.h:                           \
    backend/Backend.h:                                 \
    backend/BackendRCarDu.h:                           \
    DrmHwcTwo.h:                                       \

TIDY_FILES += \
    backend/BackendClient.cpp:COARSE                       \
    backend/Backend.cpp:COARSE                             \
    backend/BackendManager.cpp:COARSE                      \
    backend/BackendRCarDu.cpp:COARSE                       \
    bufferinfo/BufferInfoGetter.cpp:COARSE                 \
    bufferinfo/legacy/BufferInfoImagination.cpp:COARSE     \
    bufferinfo/legacy/BufferInfoLibdrm.cpp:COARSE          \
    bufferinfo/legacy/BufferInfoMaliHisi.cpp:COARSE        \
    bufferinfo/legacy/BufferInfoMaliMediatek.cpp:COARSE    \
    bufferinfo/legacy/BufferInfoMaliMeson.cpp:COARSE       \
    bufferinfo/legacy/BufferInfoMinigbm.cpp:COARSE         \
    compositor/DrmDisplayComposition.cpp:COARSE            \
    compositor/DrmDisplayCompositor.cpp:COARSE             \
    compositor/Planner.cpp:COARSE                          \
    drm/DrmConnector.cpp:COARSE                            \
    drm/DrmCrtc.cpp:COARSE                                 \
    drm/DrmDevice.cpp:COARSE                               \
    drm/DrmEncoder.cpp:COARSE                              \
    drm/DrmFbImporter.cpp:COARSE                           \
    drm/DrmMode.cpp:COARSE                                 \
    drm/DrmPlane.cpp:COARSE                                \
    drm/DrmProperty.cpp:COARSE                             \
    DrmHwcTwo.cpp:COARSE                                   \
    drm/ResourceManager.cpp:COARSE                         \
    drm/UEventListener.cpp:COARSE                          \
    drm/VSyncWorker.cpp:COARSE                             \
    tests/worker_test.cpp:COARSE                           \
    utils/autolock.cpp:COARSE                              \
    utils/Worker.cpp:COARSE                                \


.PHONY: all build tidy tidy-fine clean

all: build tidy tidy-fine

clean:
	rm -rf $(OUT_DIR)/

_OBJ := $(BUILD_FILES:.cpp=.o)
OBJ  := $(patsubst %,$(OUT_DIR)/%,$(_OBJ))

DEPS := $(patsubst %.cpp,$(OUT_DIR)/%.d,$(BUILD_FILES))

build: $(OBJ)

CXXARGS := $(foreach dir,$(INCLUDE_DIRS),-I$(SRC_DIR)/$(dir)) $(foreach dir,$(SYSTEM_INCLUDE_DIRS),-I$(dir)) $(CXXFLAGS)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CLANG) $< $(CXXARGS) -c -o $@

$(OUT_DIR)/%.d: $(SRC_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CLANG) $(CXXARGS) $< -MM -MT $(OUT_DIR)/$(patsubst %.cpp,%.o,$<) -o $@

# TIDY
space := $(subst ,, )
comma := ,

TIDY_ARGS_FINE :=
TIDY_ARGS_NONE := --checks="-*,llvm-include-order"
TIDY_ARGS_     := --checks="-*,llvm-include-order"
TIDY_ARGS_NORMAL := --checks="$(subst $(space),$(comma),$(strip $(TIDY_CHECKS_NORMAL)))"
TIDY_ARGS_COARSE := --checks="$(subst $(space),$(comma),$(strip $(TIDY_CHECKS_COARSE)))"

define process-tidy

_TARG := $(OUT_DIR)/$1.tidy.ts
_DEP := $(SRC_DIR)/$1

TIDY_DEPS += $(_TARG)

TIDY_ARGS:=$(TIDY_ARGS_$2)

$(_TARG): $(_DEP)
	mkdir -p $(dir $(_TARG))
	$(CLANG_TIDY) $(_DEP) $(TIDY_ARGS) -- -x c++ $(CXXARGS)
	touch $(_TARG)

endef

$(foreach pair,$(TIDY_FILES),$(eval $(call process-tidy,$(word 1, $(subst :, ,$(pair))),$(word 2, $(subst :, ,$(pair))))))

#$(warning $(TIDY_DEPS))

tidy: $(TIDY_DEPS)

ifneq ($(MAKECMDGOALS), clean)
-include $(DEPS)
endif
