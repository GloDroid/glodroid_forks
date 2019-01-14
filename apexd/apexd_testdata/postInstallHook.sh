#!/system/bin/sh
# Look for the session data, but do not fail and instead print a literal
# otherwise. Sleep is an attempt to ensure that the message always reaches
# logcat.
/system/bin/logwrapper /system/bin/sh -c 'ls /apex/com.android.apex.test_package/etc/sample_prebuilt_file || echo "PostInstall Test" ; sleep 5s'
