#!/bin/bash -ex

# SPDX-License-Identifier: Apache-2.0
#
# AOSPEXT project (https://github.com/GloDroid/aospext)
#
# Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)

PKG_CONFIG_LIBDIR=./pkg-config

LOCAL_PATH=`pwd`

./configure \
  --prefix=$(FFMPEG_INSTALL_DIR) \
  --libdir=$(LIBDIR) \
  --cc="/bin/bash $(MY_ABS_PATH)/aospext_cc -c c -d $(AOSP_FLAGS_DIR_OUT)" \
  --cxx="/bin/bash $(MY_ABS_PATH)/aospext_cc -c cxx -d $(AOSP_FLAGS_DIR_OUT)" \
  --nm=$(NM_TOOL) \
  --ar=$(AR_TOOL) \
  --ranlib=$(RANLIB_TOOL) \
  --yasmexe=/usr/bin/yasm \
  --arch=$(FFMPEG_ARCH) \
  ${TC_OPTIONS} \
  --enable-shared \
  --disable-static \
  ${TA_OPTIONS} \
  --enable-cross-compile \
  --target-os=android \
  --enable-pic \
  --disable-doc \
  --disable-debug \
  --disable-runtime-cpudetect \
  --disable-pthreads \
  --enable-hardcoded-tables \
  ${PROGRAM} \
  --enable-version3 \
  --disable-stripping \
  --disable-postproc \
  --disable-programs \
  --disable-ffmpeg \
  --disable-ffplay \
  --disable-ffprobe \
  --disable-network \
  --disable-iconv \
  --enable-libudev \
  --enable-libdrm \
  --enable-v4l2-request \
  --enable-decoder=mjpeg \
  --enable-parser=mjpeg \
  --enable-hwaccel=h264_v4l2request \
  --enable-hwaccel=hevc_v4l2request \
  --enable-hwaccel=vp8_v4l2request \
  --enable-hwaccel=vp9_v4l2request \
  --enable-hwaccel=mpeg2_v4l2request \
  --enable-filter=format \
  --enable-filter=hflip \
  --enable-filter=scale \
  --enable-filter=nullsink \
  --enable-filter=vflip \
  --enable-gpl \
  --extra-cflags="[C_ARGS]" \
  --extra-ldflags="[C_LINK_ARGS]" \
  --extra-cxxflags="[CPP_ARGS]" \
  --pkg-config="env PKG_CONFIG_LIBDIR=$(FFMPEG_GEN_DIR) /usr/bin/pkg-config" \
  --sysinclude=""

make install -j$(nproc --all)

#  --pkg-config-flags="PKG_CONFIG_LIBDIR=./pkg-config /usr/bin/pkg-config" \
#  --enable-libv4l2 \
