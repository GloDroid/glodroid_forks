
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

TIDY_FILES_H := \
    drm/DrmFbImporter.h \
    drm/DrmUnique.h \
    utils/UniqueFd.h \
    utils/log.h \
    utils/properties.h \

TIDY_FILES_CPP := \

TIDY_COARSE_CHECKS := * \
    -hicpp-* -llvmlibc-* -fuchsia-* -altera-* \
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

.PHONY: all build tidy tidy-fine clean

all: build tidy tidy-fine

clean:
	rm -rf $(OUT_DIR)/

_OBJ := $(BUILD_FILES:.cpp=.o)
OBJ  := $(patsubst %,$(OUT_DIR)/%,$(_OBJ))

_TIDY_TS := $(BUILD_FILES:.cpp=.cpp.t_ts)
TIDY_TS := $(patsubst %,$(OUT_DIR)/%,$(_TIDY_TS))

_FINE_TIDY_TS := $(TIDY_FILES_CPP:.cpp=.cpp,ft_ts)
_FINE_TIDY_TS += $(TIDY_FILES_H:.h=.h.ft_ts)
FINE_TIDY_TS  := $(patsubst %,$(OUT_DIR)/%,$(_FINE_TIDY_TS))

DEPS := $(patsubst %.cpp,$(OUT_DIR)/%.d,$(BUILD_FILES))

build: $(OBJ)

tidy: $(TIDY_TS)

tidy-fine: $(FINE_TIDY_TS)

CXXARGS := $(foreach dir,$(INCLUDE_DIRS),-I$(SRC_DIR)/$(dir)) $(foreach dir,$(SYSTEM_INCLUDE_DIRS),-I$(dir)) $(CXXFLAGS)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CLANG) $< $(CXXARGS) -c -o $@

$(OUT_DIR)/%.d: $(SRC_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CLANG) $(CXXARGS) $< -MM -MT $(patsubst %.cpp,%.o,$<) -o $@

space := $(subst ,, )
comma := ,

# TIDY
define exec-tidy
	mkdir -p $(dir $@)
	$(CLANG_TIDY) $< --checks="$(subst $(space),$(comma),$(strip $(TIDY_COARSE_CHECKS)))" -- -x c++ $(CXXARGS)
	touch $@
endef

$(OUT_DIR)/%.h.t_ts: $(SRC_DIR)/%.h $(OBJ)
	$(exec-tidy)

$(OUT_DIR)/%.cpp.t_ts: $(SRC_DIR)/%.cpp $(OBJ)
	$(exec-tidy)

# TIDY-FINE
define exec-tidy-fine
	mkdir -p $(dir $@)
	$(CLANG_TIDY) $< -- -x c++ $(CXXARGS)
	touch $@
endef

$(OUT_DIR)/%.cpp.ft_ts: $(SRC_DIR)/%.cpp $(OBJ)
	$(exec-tidy-fine)

$(OUT_DIR)/%.h.ft_ts: $(SRC_DIR)/%.h $(OBJ)
	$(exec-tidy-fine)

ifneq ($(MAKECMDGOALS), clean)
-include $(DEPS)
endif
