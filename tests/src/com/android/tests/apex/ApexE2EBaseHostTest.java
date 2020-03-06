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

import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assume.assumeTrue;

import com.android.tests.util.ModuleTestUtils;
import com.android.tradefed.config.Option;
import com.android.tradefed.config.Option.Importance;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;

import java.io.File;
import java.io.IOException;
import java.time.Duration;
import java.util.Set;

/**
 * Base test to check if Apex can be staged, activated and uninstalled successfully.
 */
public abstract class ApexE2EBaseHostTest extends BaseHostJUnit4Test {

    private static final String OPTION_APEX_FILE_NAME = "apex_file_name";

    private static final Duration WAIT_FOR_BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(2);

    /* protected so that derived tests can have access to test utils automatically */
    protected final ModuleTestUtils mUtils = new ModuleTestUtils(this);

    @Option(name = OPTION_APEX_FILE_NAME,
            description = "The file name of the apex module.",
            importance = Importance.IF_UNSET,
            mandatory = true
    )
    protected String mApexFileName = null;
    protected boolean mApexUpdatable = true;

    @Before
    public void setUp() throws Exception {
        assumeTrue("Updating APEX is not supported", mUtils.isApexUpdateSupported());
        mUtils.abandonActiveStagedSession();
        uninstallApex();
    }

    /**
     * Check if Apex package can be staged, activated and uninstalled successfully.
     */
    @Test
    public final void testStageActivateUninstallApexPackage()  throws Exception {

        File testAppFile = mUtils.getTestFile(mApexFileName);
        CLog.i("Found test apex file: " + testAppFile.getAbsoluteFile());

        // Install apex package
        String installResult = getDevice().installPackage(testAppFile, false, "--wait");
        Assert.assertNull(
                String.format("failed to install test app %s. Reason: %s",
                    mApexFileName, installResult),
                installResult);

        ApexInfo testApexInfo = mUtils.getApexInfo(testAppFile);
        Assert.assertNotNull(testApexInfo);

        getDevice().reboot(); // for install to take affect
        assertWithMessage("Device didn't boot in %s", WAIT_FOR_BOOT_COMPLETE_TIMEOUT).that(
                getDevice().waitForBootComplete(
                        WAIT_FOR_BOOT_COMPLETE_TIMEOUT.toMillis())).isTrue();
        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        Assert.assertTrue(
                String.format("Failed to activate %s %s",
                    testApexInfo.name, testApexInfo.versionCode),
                activatedApexes.contains(testApexInfo));

        additionalCheck();
    }

    /**
     * Do some additional check, invoked by {@link #testStageActivateUninstallApexPackage()}.
     */
    public void additionalCheck() throws Exception {};

    @After
    public void tearDown() throws Exception {
        assumeTrue("Updating APEX is not supported", mUtils.isApexUpdateSupported());
        mUtils.abandonActiveStagedSession();
        uninstallApex();
    }

    private void uninstallApex() throws DeviceNotAvailableException, IOException {
        ApexInfo apex = mUtils.getApexInfo(mUtils.getTestFile(mApexFileName));
        String uninstallResult = getDevice().uninstallPackage(apex.name);
        if (uninstallResult != null) {
            // Uninstall failed. Most likely this means that there were no apex installed. No need
            // to reboot.
            CLog.w("Failed to uninstall apex " + apex.name + " : " + uninstallResult);
        } else {
            // Uninstall succeeded. Need to reboot.
            getDevice().reboot(); // for the uninstall to take affect
        }
    }
}
