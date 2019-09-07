/*
 * Copyright (C) 2019 The Android Open Source Project
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

package com.android.tests.util;

import com.android.tradefed.build.BuildInfoKey.BuildInfoFileKey;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.android.tradefed.util.FileUtil;
import com.android.tradefed.util.IRunUtil;
import com.android.tradefed.util.RunUtil;
import com.android.tradefed.util.SystemUtil.EnvVariable;

import org.junit.Assert;

import java.io.File;
import java.io.IOException;
import java.time.Duration;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

public class ModuleTestUtils {

    private static final String APEX_INFO_EXTRACT_REGEX =
            ".*package:\\sname='(\\S+)\\'\\sversionCode='(\\d+)'\\s.*";

    private static final Duration WAIT_FOR_SESSION_READY_TTL = Duration.ofSeconds(10);
    private static final Duration SLEEP_FOR = Duration.ofMillis(200);

    protected final Pattern mIsSessionReadyPattern =
            Pattern.compile("(isReady = true)|(isStagedSessionReady = true)");
    protected final Pattern mIsSessionAppliedPattern =
            Pattern.compile("(isApplied = true)|(isStagedSessionApplied = true)");

    private IRunUtil mRunUtil = new RunUtil();
    private BaseHostJUnit4Test mTest;

    private IBuildInfo getBuild() {
        return mTest.getBuild();
    }

    public ModuleTestUtils(BaseHostJUnit4Test test) {
        mTest = test;
    }

    /**
     * Retrieve package name and version code from test apex file.
     *
     * @param apex input apex file to retrieve the info from
     */
    public ApexInfo getApexInfo(File apex) {
        String aaptOutput = runCmd(String.format(
                "aapt dump badging %s", apex.getAbsolutePath()));
        String[] lines = aaptOutput.split("\n");
        Pattern p = Pattern.compile(APEX_INFO_EXTRACT_REGEX);
        for (String l : lines) {
            Matcher m = p.matcher(l);
            if (m.matches()) {
                ApexInfo apexInfo = new ApexInfo(m.group(1), Long.parseLong(m.group(2)));
                return apexInfo;
            }
        }
        return null;
    }

    /**
     * Get the test file.
     *
     * @param testFileName name of the file
     */
    public File getTestFile(String testFileName) throws IOException {
        File testFile = null;

        String testcasesPath = System.getenv(EnvVariable.ANDROID_HOST_OUT_TESTCASES.toString());
        if (testcasesPath != null) {
            testFile = searchTestFile(new File(testcasesPath), testFileName);
        }
        if (testFile != null) {
            return testFile;
        }

        File hostLinkedDir = getBuild().getFile(BuildInfoFileKey.HOST_LINKED_DIR);
        if (hostLinkedDir != null) {
            testFile = searchTestFile(hostLinkedDir, testFileName);
        }
        if (testFile != null) {
            return testFile;
        }

        // Find the file in the buildinfo.
        File buildInfoFile = getBuild().getFile(testFileName);
        if (buildInfoFile != null) {
            return buildInfoFile;
        }

        throw new IOException("Cannot find " + testFileName);
    }

    private String runCmd(String cmd) {
        CLog.d("About to run command: %s", cmd);
        CommandResult result = mRunUtil.runTimedCmd(1000 * 60 * 5, cmd.split("\\s+"));
        Assert.assertNotNull(result);
        Assert.assertTrue(
                String.format("Command %s failed", cmd),
                result.getStatus().equals(CommandStatus.SUCCESS));
        CLog.v("output:\n%s", result.getStdout());
        return result.getStdout();
    }

    /**
     * Searches the file with the given name under the given directory, returns null if not found.
     */
    private File searchTestFile(File baseSearchFile, String testFileName) {
        if (baseSearchFile != null && baseSearchFile.isDirectory()) {
            File testFile = FileUtil.findFile(baseSearchFile, testFileName);
            if (testFile != null && testFile.isFile()) {
                return testFile;
            }
        }
        return null;
    }

    public void waitForStagedSessionReady() throws DeviceNotAvailableException {
        // TODO: implement wait for session ready logic inside PackageManagerShellCommand instead.
        boolean sessionReady = false;
        Duration spentWaiting = Duration.ZERO;
        while (spentWaiting.compareTo(WAIT_FOR_SESSION_READY_TTL) < 0) {
            CommandResult res = mTest.getDevice().executeShellV2Command("pm get-stagedsessions");
            Assert.assertEquals("", res.getStderr());
            sessionReady = Stream.of(res.getStdout().split("\n")).anyMatch(this::isReadyNotApplied);
            if (sessionReady) {
                CLog.i("Done waiting after " + spentWaiting);
                break;
            }
            try {
                Thread.sleep(SLEEP_FOR.toMillis());
                spentWaiting = spentWaiting.plus(SLEEP_FOR);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(e);
            }
        }
        Assert.assertTrue("Staged session wasn't ready in " + WAIT_FOR_SESSION_READY_TTL,
                sessionReady);
    }

    private boolean isReadyNotApplied(String sessionInfo) {
        boolean isReady = mIsSessionReadyPattern.matcher(sessionInfo).find();
        boolean isApplied = mIsSessionAppliedPattern.matcher(sessionInfo).find();
        return isReady && !isApplied;
    }
}
