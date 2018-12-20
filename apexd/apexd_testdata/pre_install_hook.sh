#!/system/bin/sh
/system/bin/logwrapper /system/bin/sh -c 'ls /apex/com.android.apex.test_package/etc/sample_prebuilt_file || echo "PreInstall Test"'
