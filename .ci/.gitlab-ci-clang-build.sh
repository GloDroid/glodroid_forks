#!/bin/bash

. ./.ci/.common.sh

BUILD_FILES=(
drm/DrmConnector.cpp
drm/DrmCrtc.cpp
drm/DrmDevice.cpp
drm/DrmEncoder.cpp
drm/DrmEventListener.cpp
drm/DrmMode.cpp
drm/DrmProperty.cpp
utils/Worker.cpp
)

set -xe

for source in "${BUILD_FILES[@]}"
do
    filename=$(basename -- "$source")
    $CLANG $source $INCLUDE_DIRS $CXXARGS -c -o /tmp/"${filename%.*}.o"
done
