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
import com.android.tradefed.config.Option;
import com.android.tradefed.config.Option.Importance;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.FileUtil;
import com.android.tradefed.util.SystemUtil.EnvVariable;

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
    private static final String APEX_DATA_DIR = "/data/apex";
    private static final String STAGING_DATA_DIR = "/data/staging";
    private static final String OPTION_APEX_PACKAGE_NAME = "apex_package_name";
    private static final String OPTION_APEX_FILE_NAME = "apex_file_name";
    private static final String OPTION_APEX_VERSION = "apex_version_code";

    private boolean mAdbWasRoot;

    @Option(name = OPTION_APEX_PACKAGE_NAME,
            description = "The package name of the apex module. Specified in manifest.json.",
            importance = Importance.IF_UNSET,
            mandatory = true
    )
    private String mApexPackageName = null;

    @Option(name = OPTION_APEX_FILE_NAME,
            description = "The file name of the apex module.",
            importance = Importance.IF_UNSET,
            mandatory = true
    )
    private String mApexFileName = null;

    @Option(name = OPTION_APEX_VERSION,
            description = "The version code of the apex module.",
            importance = Importance.IF_UNSET,
            mandatory = true
    )
    private long mApexVersion;


    @Before
    public synchronized void setUp() throws Exception {
        getDevice().executeShellV2Command("rm -rf " + APEX_DATA_DIR + "/*");
        getDevice().executeShellV2Command("rm -rf " + STAGING_DATA_DIR + "/*");
    }

    /**
     * Check if Apex package can be staged, activated and uninstalled successfully.
     */
    public void doTestStageActivateUninstallApexPackage()
                                throws DeviceNotAvailableException, IOException {

        File testAppFile = getTestApex();
        CLog.i("Found test apex file: " + testAppFile.getAbsoluteFile());

        String installResult = getDevice().installPackage(testAppFile, false);
        Assert.assertNull(
                String.format("failed to install test app %s. Reason: %s",
                    mApexFileName, installResult),
                installResult);

        getDevice().reboot();

        // Check that the APEX is actually activated
        ApexInfo testApexInfo = new ApexInfo(mApexPackageName, mApexVersion);
        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        Assert.assertTrue(
                String.format("Failed to activate %s", mApexPackageName),
                activatedApexes.contains(testApexInfo));

        additionalCheck();
    }

    /**
     * Do some additional check, invoked by doTestStageActivateUninstallApexPackage.
     */
    public abstract void additionalCheck();

    /**
     * Helper method to get the test apex.
     */
    private File getTestApex() throws IOException {
        File apexFile = null;

        String testcasesPath = System.getenv(EnvVariable.ANDROID_HOST_OUT_TESTCASES.toString());
        if (testcasesPath != null) {
            apexFile = searchApexFile(new File(testcasesPath), mApexFileName);
        }
        if (apexFile != null) {
            return apexFile;
        }

        File hostLinkedDir = getBuild().getFile(BuildInfoFileKey.HOST_LINKED_DIR);
        if (hostLinkedDir != null) {
            apexFile = searchApexFile(hostLinkedDir, mApexFileName);
        }
        if (apexFile != null) {
            return apexFile;
        }

        // Find the apex file in the buildinfo.
        File tzdataFile = getBuild().getFile(mApexFileName);
        if (tzdataFile != null) {
            return tzdataFile;
        }

        throw new IOException("Cannot find " + mApexFileName);
    }

    /**
     * Searches the file with the given name under the given directory, returns null if not found.
     */
    private File searchApexFile(File baseSearchFile, String apexFileName) {
        if (baseSearchFile != null && baseSearchFile.isDirectory()) {
            File apexFile = FileUtil.findFile(baseSearchFile, apexFileName);
            if (apexFile != null && apexFile.isFile()) {
                return apexFile;
            }
        }
        return null;
    }

    @After
    public void tearDown() throws DeviceNotAvailableException {
        getDevice().executeShellV2Command("rm -rf " + APEX_DATA_DIR + "/*");
        getDevice().executeShellV2Command("rm -rf " + STAGING_DATA_DIR + "/*");
        getDevice().reboot();
    }
}
