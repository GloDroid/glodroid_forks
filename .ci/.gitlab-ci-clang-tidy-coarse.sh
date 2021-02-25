#!/bin/bash

. ./.ci/.common.sh

TIDY_FILES=(
)

set -xe

for source in "${TIDY_FILES[@]}"
do
    $CLANG_TIDY $source -- -x c++ $INCLUDE_DIRS
done
