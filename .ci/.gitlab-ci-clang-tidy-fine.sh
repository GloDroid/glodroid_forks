#!/bin/bash

. ./.ci/.common.sh

TIDY_FILES=(
utils/gralloc.h
utils/log.h
utils/properties.h
)

set -xe

for source in "${TIDY_FILES[@]}"
do
    $CLANG_TIDY $source -- -x c++ $INCLUDE_DIRS
done
