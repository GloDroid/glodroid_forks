#!/bin/bash -e

adb root && adb remount

make all

adb push ./install/vendor/etc/init/android.hardware.graphics.allocator@4.0-service.minigbm.rc /vendor/etc/init/
adb push ./install/vendor/bin/hw/android.hardware.graphics.allocator@4.0-service.minigbm_gd /vendor/bin/hw/
adb push ./install/vendor/lib64/libminigbm_gralloc_gd.so /vendor/lib64/
adb push ./install/vendor/lib64/hw/gralloc.minigbm_gd.so /vendor/lib64/hw/
adb push ./install/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl.minigbm_gd.so /vendor/lib64/hw/

adb shell stop
adb shell stop vendor.graphics.allocator-4-0
adb shell start vendor.graphics.allocator-4-0
adb shell start
