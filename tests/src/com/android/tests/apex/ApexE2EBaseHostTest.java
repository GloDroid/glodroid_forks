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

import com.android.tradefed.config.Option;
import com.android.tradefed.config.Option.Importance;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;

import java.io.File;
import java.io.IOException;
import java.util.Set;

/**
 * Base test to check if Apex can be staged, activated and uninstalled successfully.
 */
public abstract class ApexE2EBaseHostTest extends BaseHostJUnit4Test {
    private static final String SUPPORT_APEX_UPDATE_PROPERTY = "ro.apex.updatable";

    protected static final String OPTION_APEX_FILE_NAME = "apex_file_name";

    /* protected so that derived tests can have access to test utils automatically */
    protected final ApexTestUtils mUtils = new ApexTestUtils(this);

    @Option(name = OPTION_APEX_FILE_NAME,
            description = "The file name of the apex module.",
            importance = Importance.IF_UNSET,
            mandatory = true
    )
    protected String mApexFileName = null;
    protected boolean mApexUpdatable = true;

    @Before
    public synchronized void setUp() throws Exception {
        if (getDevice().getProperty(SUPPORT_APEX_UPDATE_PROPERTY).equals("false")) {
            mApexUpdatable = false;
            CLog.i("Apex updating is not supported on this device. Skipping setup().");
            return;
        }
        uninstallApex();
    }

    /**
     * Check if Apex package can be staged, activated and uninstalled successfully.
     */
    public void doTestStageActivateUninstallApexPackage()
                                throws DeviceNotAvailableException, IOException {

        if (!mApexUpdatable) {
            CLog.i("Apex updating is not supported on this device. Skipping test.");
            return;
        }

        File testAppFile = mUtils.getTestFile(mApexFileName);
        CLog.i("Found test apex file: " + testAppFile.getAbsoluteFile());

        // Install apex package
        String installResult = getDevice().installPackage(testAppFile, false);
        Assert.assertNull(
                String.format("failed to install test app %s. Reason: %s",
                    mApexFileName, installResult),
                installResult);

        mUtils.waitForStagedSessionReady();
        ApexInfo testApexInfo = mUtils.getApexInfo(testAppFile);
        Assert.assertNotNull(testApexInfo);

        getDevice().reboot(); // for install to take affect

        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        Assert.assertTrue(
                String.format("Failed to activate %s %s",
                    testApexInfo.name, testApexInfo.versionCode),
                activatedApexes.contains(testApexInfo));

        additionalCheck();
    }

    /**
     * Do some additional check, invoked by doTestStageActivateUninstallApexPackage.
     */
    public abstract void additionalCheck();

    @After
    public void tearDown() throws Exception {
        if (!mApexUpdatable) {
            CLog.i("Apex updating is not supported on this device. Skipping teardown().");
            return;
        }
        uninstallApex();
    }

    private void uninstallApex() throws Exception {
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
