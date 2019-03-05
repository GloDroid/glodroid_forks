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
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.util.CommandResult;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.IOException;
import java.util.Set;

/**
 * Test to check if Apex can be staged, activated and uninstalled successfully.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexPackageStageActivateUninstallHostTest extends ApexE2EBaseHostTest {

    /**
     * Tests that if Apex package can be staged, activated and uninstalled successfully.
     */
    @Test
    public void testStageActivateUninstallApexPackage()
                                throws DeviceNotAvailableException, IOException {
        doTestStageActivateUninstallApexPackage();
    }

    @Test
    public void testStageActivatUninstallTwice() throws Exception {
        doTestStageActivateUninstallApexPackage();
        CLog.i("Installing the same apex second time");
        doTestStageActivateUninstallApexPackage();
    }

    @Test
    public void testAbandonStagedSessionBeforeReboot()
                                throws DeviceNotAvailableException, IOException {
        File testAppFile = mUtils.getTestFile(mApexFileName);
        CLog.i("Found test apex file: " + testAppFile.getAbsoluteFile());

        // Install apex package
        String installResult = getDevice().installPackage(testAppFile, false);
        Assert.assertNull(
                String.format("failed to install test app %s. Reason: %s",
                mApexFileName, installResult),
                installResult);

        try {
            Thread.sleep(10000);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        ApexInfo testApexInfo = mUtils.getApexInfo(testAppFile);
        Assert.assertNotNull(testApexInfo);

        // Assert isSessionReady is true
        CommandResult result = getDevice().executeShellV2Command("pm get-stagedsessions");
        Assert.assertEquals("", result.getStderr());
        mUtils.assertMatchesRegex(result.getStdout(), mIsSessionReadyPattern);

        // Assert there is only 1 staged session in flight
        Assert.assertEquals(1, result.getStdout().split("\n").length);

        // Abandon session
        String stagedSessionId = result.getStdout().split(";")[1 /* session id is 2nd param */]
                .split(" = ")[1];
        getDevice().executeShellV2Command("pm install-abandon " + stagedSessionId);

        // Check session is abandoned
        result = getDevice().executeShellV2Command("pm get-stagedsessions");
        Assert.assertEquals("", result.getStderr());
        Assert.assertEquals("", result.getStdout());

        // Reboot
        getDevice().reboot();

        // Check session was not activated
        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        Assert.assertFalse(
                String.format("Failed to abandon %s %s",
                testApexInfo.name, testApexInfo.versionCode),
                activatedApexes.contains(testApexInfo));

        additionalCheck();
    }

    @Override
    public void additionalCheck() {
      // Nothing to do here.
    }
}
