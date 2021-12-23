
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
    -cppcoreguidelines-non-private-member-variables-in-classes \
    -cppcoreguidelines-pro-bounds-array-to-pointer-decay \
    -cppcoreguidelines-pro-bounds-constant-array-index \
    -cppcoreguidelines-pro-bounds-pointer-arithmetic \
    -cppcoreguidelines-pro-type-cstyle-cast \
    -cppcoreguidelines-pro-type-reinterpret-cast \
    -cppcoreguidelines-pro-type-static-cast-downcast \
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
    -modernize-use-nodiscard \
    -readability-braces-around-statements \
    -readability-convert-member-functions-to-static \
    -readability-implicit-bool-conversion \
    -readability-identifier-naming \
    -readability-magic-numbers \
    -readability-use-anyofallof \

TIDY_FILES := \
    drm/UEventListener.h:COARSE                        \
    drm/DrmFbImporter.h:FINE                           \
    drm/ResourceManager.h:COARSE                       \
    drm/DrmMode.h:COARSE                               \
    drm/DrmDevice.h:COARSE                             \
    drm/DrmProperty.h:COARSE                           \
    drm/DrmConnector.h:COARSE                          \
    drm/DrmCrtc.h:COARSE                               \
    drm/VSyncWorker.h:COARSE                           \
    drm/DrmPlane.h:COARSE                              \
    drm/DrmUnique.h:FINE                               \
    drm/DrmEncoder.h:COARSE                            \
    bufferinfo/legacy/BufferInfoMinigbm.h:COARSE       \
    bufferinfo/legacy/BufferInfoLibdrm.h:COARSE        \
    bufferinfo/legacy/BufferInfoMaliMediatek.h:COARSE  \
    bufferinfo/legacy/BufferInfoMaliHisi.h:COARSE      \
    bufferinfo/legacy/BufferInfoImagination.h:COARSE   \
    bufferinfo/legacy/BufferInfoMaliMeson.h:COARSE     \
    bufferinfo/BufferInfoGetter.h:COARSE               \
    bufferinfo/BufferInfoMapperMetadata.h:COARSE       \
    include/drmhwcgralloc.h:COARSE                     \
    include/drmhwcomposer.h:COARSE                     \
    utils/Worker.h:COARSE                              \
    utils/UniqueFd.h:FINE                              \
    utils/autolock.h:COARSE                            \
    utils/log.h:FINE                                   \
    utils/properties.h:FINE                            \
    compositor/DrmDisplayCompositor.h:COARSE           \
    compositor/Planner.h:COARSE                        \
    compositor/DrmDisplayComposition.h:COARSE          \
    backend/BackendManager.h:COARSE                    \
    backend/BackendClient.h:COARSE                     \
    backend/Backend.h:COARSE                           \
    backend/BackendRCarDu.h:COARSE                     \
    DrmHwcTwo.h:COARSE                                 \

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
