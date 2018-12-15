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

import com.android.tradefed.build.BuildInfoKey.BuildInfoFileKey;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.android.tradefed.util.FileUtil;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.IOException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Test to check if Apex can be staged, activated and uninstalled successfully.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexPackageStageActivateUninstallHostTest extends BaseHostJUnit4Test {

    private static final String TEST_APEX_FILE = "test.apex";
    private static final String TEST_PACKAGE_NAME = "com.android.apex.test";
    private static final String APEX_DATA_DIR = "/data/apex";
    private boolean mAdbWasRoot;

    @Before
    public synchronized void setUp() throws Exception {
        getDevice().executeShellV2Command("rm -rf " + APEX_DATA_DIR + "/*");
        mAdbWasRoot = getDevice().isAdbRoot();
    }

    /**
     * Tests that if Apex package can be staged, activated and uninstalled successfully.
     */
    @Test
    public void testStageActivateUninstallApexPackage()
                                throws DeviceNotAvailableException, IOException {

        // Test staging APEX module (currently we simply install a sample apk).
        // TODO: change sample apk to test APEX package and do install using adb.
        File testAppFile = getTestApex();
        String installResult = getDevice().installPackage(testAppFile, false);
        Assert.assertNull(
                String.format("failed to install test app. Reason: %s", installResult),
                installResult);

        // TODO: ensure that APEX is staged.

        // Reboot to actually activate the staged APEX.
        getDevice().nonBlockingReboot();
        getDevice().waitForDeviceAvailable();

        // Disable SELinux to directly talk to apexservice.
        // TODO: Remove this by using Package Manager APIs instead.
        final boolean adbWasRoot = getDevice().isAdbRoot();
        if (!adbWasRoot) {
            Assert.assertTrue(getDevice().enableAdbRoot());
        }
        CommandResult result = getDevice().executeShellV2Command("getenforce");
        final boolean selinuxWasEnforcing = result.getStdout().contains("Enforcing");
        if (selinuxWasEnforcing) {
            result = getDevice().executeShellV2Command("setenforce 0");
            Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        }

        // Check that the APEX is actually activated
        result = getDevice().executeShellV2Command("cmd apexservice getActivePackages");
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        String lines[] = result.getStdout().split("\n");
        Pattern p = Pattern.compile("Package:\\ ([\\S]+)\\ Version:\\ ([\\d]+)");
        boolean found = false;
        for (String l : lines) {
            Matcher m = p.matcher(l);
            Assert.assertTrue(m.matches());
            String name = m.group(1);
            if (name.equals(TEST_PACKAGE_NAME)) {
                found = true;
                break;
            }
        }
        Assert.assertTrue(found);

        // Enable SELinux back on
        // TODO: don't change SElinux enforcement at all
        if (selinuxWasEnforcing) {
            result = getDevice().executeShellV2Command("setenforce 1");
            Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        }
        if (!adbWasRoot) {
            Assert.assertTrue(getDevice().disableAdbRoot());
        }
    }

    /**
     * Helper method to get the test apex.
     */
    private File getTestApex() throws IOException {
        File testDir = new File(
                getBuild().getFile(BuildInfoFileKey.HOST_LINKED_DIR),
                "apex_e2e_tests");
        return FileUtil.findFile(testDir, TEST_APEX_FILE);
    }

    @After
    public void tearDown() throws DeviceNotAvailableException {
        Assert.assertTrue(getDevice().enableAdbRoot());
        getDevice().executeShellV2Command("rm -rf " + APEX_DATA_DIR + "/*");
        if (!mAdbWasRoot) {
            Assert.assertTrue(getDevice().disableAdbRoot());
        }
        getDevice().reboot();
    }
}
