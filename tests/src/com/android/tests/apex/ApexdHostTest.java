/*
 * Copyright (C) 2020 The Android Open Source Project
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

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assume.assumeTrue;

import com.android.tests.util.ModuleTestUtils;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import com.google.common.base.Stopwatch;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.time.Duration;
import java.util.Set;

/**
 * Host side integration tests for apexd.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexdHostTest extends BaseHostJUnit4Test  {

    private final ModuleTestUtils mTestUtils = new ModuleTestUtils(this);

    @Test
    public void testOrphanedApexIsNotActivated() throws Exception {
        assumeTrue("Device requires root", getDevice().isAdbRoot());
        try {
            assertThat(getDevice().pushFile(mTestUtils.getTestFile("apex.apexd_test_v2.apex"),
                    "/data/apex/active/apexd_test_v2.apex")).isTrue();
            getDevice().reboot();
            assertWithMessage("Timed out waiting for device to boot").that(
                    getDevice().waitForBootComplete(Duration.ofMinutes(2).toMillis())).isTrue();
            final Set<ITestDevice.ApexInfo> activeApexes = getDevice().getActiveApexes();
            ITestDevice.ApexInfo testApex = new ITestDevice.ApexInfo(
                    "com.android.apex.test_package", 2L);
            assertThat(activeApexes).doesNotContain(testApex);
            waitForFileDeleted("/data/apex/active/apexd_test_v2.apex", Duration.ofMinutes(1));
        } finally {
            getDevice().executeShellV2Command("rm /data/apex/active/apexd_test_v2.apex");
        }
    }
    @Test
    public void testApexWithoutPbIsNotActivated() throws Exception {
        final String testApexFile = "com.android.apex.cts.shim.v2_no_pb.apex";
        assumeTrue("Device requires root", getDevice().isAdbRoot());
        try {
            assertThat(getDevice().pushFile(mTestUtils.getTestFile(testApexFile),
                    "/data/apex/active/" + testApexFile)).isTrue();
            getDevice().reboot();
            assertWithMessage("Timed out waiting for device to boot").that(
                    getDevice().waitForBootComplete(Duration.ofMinutes(2).toMillis())).isTrue();
            final Set<ITestDevice.ApexInfo> activeApexes = getDevice().getActiveApexes();
            ITestDevice.ApexInfo testApex = new ITestDevice.ApexInfo(
                    "com.android.apex.cts.shim", 2L);
            assertThat(activeApexes).doesNotContain(testApex);
            waitForFileDeleted("/data/apex/active/" + testApexFile, Duration.ofMinutes(1));
        } finally {
            getDevice().executeShellV2Command("rm /data/apex/active/" + testApexFile);
        }
    }

    private void waitForFileDeleted(String filePath, Duration timeout) throws Exception {
        Stopwatch stopwatch = Stopwatch.createStarted();
        while (true) {
            if (!getDevice().doesFileExist(filePath)) {
                return;
            }
            if (stopwatch.elapsed().compareTo(timeout) > 0) {
                break;
            }
            Thread.sleep(500);
        }
        throw new AssertionError("Timed out waiting for " + filePath + " to be deleted");
    }
}
