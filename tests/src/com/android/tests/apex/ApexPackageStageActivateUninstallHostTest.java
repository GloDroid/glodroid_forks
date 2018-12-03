/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.tests.apex;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.build.BuildInfoKey.BuildInfoFileKey;
import com.android.tradefed.util.FileUtil;

import java.io.File;
import java.io.IOException;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Test to check if Apex can be staged, activated and uninstalled successfully.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexPackageStageActivateUninstallHostTest extends BaseHostJUnit4Test {

    private static final String TEST_APK_NAME = "HelloActivity";
    private static final String TEST_PACKAGE_NAME = "com.example.android.helloactivity";

    private ITestDevice mDevice;

    @Before
    public synchronized void setUp() throws Exception {
        // Cleanup test apps that might be installed from previous partial test run
        mDevice = getDevice();
        mDevice.uninstallPackage(TEST_PACKAGE_NAME);
    }

    /**
     * Tests that if Apex package can be staged, activated and uninstalled successfully.
     */
    @Test
    public void testStageActivateUninstallApexPackage()
                                throws DeviceNotAvailableException, IOException {

        // Test staging APEX module (currently we simply install a sample apk).
        // TODO: change sample apk to test APEX package and do install using adb.
        File testAppFile = getTestApk();
        String installResult = mDevice.installPackage(testAppFile, false);
        Assert.assertNull(
                String.format("failed to install test app. Reason: %s", installResult),
                installResult);
        Assert.assertTrue(mDevice.getInstalledPackageNames().contains(TEST_PACKAGE_NAME));

        // TODO: Reboot device after staging to test if apex can be activated successfully.

        // Test uninstallation
        // TODO: reboot device to check the Apexd activates the original module
        // after uninstallation.
        mDevice.uninstallPackage(TEST_PACKAGE_NAME);
        Assert.assertFalse(mDevice.getInstalledPackageNames().contains(TEST_PACKAGE_NAME));
    }

    /**
     * Helper method to extract the sample apk from the jar file.
     */
    private File getTestApk() throws IOException {
        File hostdir = getBuild().getFile(BuildInfoFileKey.HOST_LINKED_DIR);
        File apkFile = FileUtil.findFile(hostdir, TEST_APK_NAME + ".apk");
        return apkFile;
    }

    @After
    public void tearDown() throws DeviceNotAvailableException {
        mDevice.uninstallPackage(TEST_PACKAGE_NAME);
        mDevice.reboot();
    }
}


