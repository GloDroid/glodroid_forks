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
import com.android.tradefed.util.CommandResult;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;

import java.io.File;
import java.io.IOException;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * Base test to check if Apex can be staged, activated and uninstalled successfully.
 */
public abstract class ApexE2EBaseHostTest extends BaseHostJUnit4Test {
    protected static final String APEX_DATA_DIR = "/data/apex";
    protected static final String STAGING_DATA_DIR = "/data/app-staging";
    protected static final String OPTION_APEX_FILE_NAME = "apex_file_name";

    protected final Pattern mAppPackageNamePattern =
            Pattern.compile("appPackageName = com\\.android\\.apex\\.test;");
    protected final Pattern mIsSessionReadyPattern = Pattern.compile("isStagedSessionReady = true");
    protected final Pattern mIsSessionAppliedPattern =
            Pattern.compile("isStagedSessionApplied = true;");

    /* protected so that derived tests can have access to test utils automatically */
    protected final ApexTestUtils mUtils = new ApexTestUtils(this);

    @Option(name = OPTION_APEX_FILE_NAME,
            description = "The file name of the apex module.",
            importance = Importance.IF_UNSET,
            mandatory = true
    )
    protected String mApexFileName = null;

    @Before
    public synchronized void setUp() throws Exception {
        getDevice().executeShellV2Command("rm -rf " + APEX_DATA_DIR + "/*");
        getDevice().executeShellV2Command("rm -rf " + STAGING_DATA_DIR + "/*");
        getDevice().reboot(); // for the above commands to take affect
    }

    /**
     * Check if Apex package can be staged, activated and uninstalled successfully.
     */
    public void doTestStageActivateUninstallApexPackage()
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

        // Assert isStagedSessionReady is true
        CommandResult result = getDevice().executeShellV2Command("pm get-stagedsessions");
        Assert.assertEquals("", result.getStderr());
        // TODO: Look into why appPackageInfo is null? or should it be null?
        // assertMatchesRegex(result.getStdout(), mAppPackageNamePattern);
        mUtils.assertMatchesRegex(result.getStdout(), mIsSessionReadyPattern);

        getDevice().reboot();

        // This checks that the staged package was activated on reboot
        result = getDevice().executeShellV2Command("pm get-stagedsessions");
        Assert.assertEquals("", result.getStderr());
        mUtils.assertMatchesRegex(result.getStdout(), mIsSessionAppliedPattern);

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
    public void tearDown() throws DeviceNotAvailableException {
        getDevice().executeShellV2Command("rm -rf " + APEX_DATA_DIR + "/*");
        getDevice().executeShellV2Command("rm -rf " + STAGING_DATA_DIR + "/*");
        getDevice().reboot();
    }
}
