INCLUDE_DIRS="-I. -I../libdrm/include/drm -Iinclude -I/usr/include/libdrm -I./.ci/android_headers"

CLANG="clang++-11"
CLANG_TIDY="clang-tidy-11"

CXXARGS="-fPIC -Wall -Werror -DPLATFORM_SDK_VERSION=30 -Wsign-promo -Wimplicit-fallthrough"
CXXARGS+=" -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Wno-gnu-include-next "
CXXARGS+=" -fvisibility-inlines-hidden -std=gnu++17 -DHWC2_USE_CPP11 -DHWC2_INCLUDE_STRINGIFICATION -fexceptions -fno-rtti"

BUILD_FILES=(
bufferinfo/BufferInfoGetter.cpp
drm/DrmConnector.cpp
drm/DrmCrtc.cpp
drm/DrmDevice.cpp
drm/DrmEncoder.cpp
drm/DrmEventListener.cpp
drm/DrmMode.cpp
drm/DrmPlane.cpp
drm/DrmProperty.cpp
utils/Worker.cpp
)
