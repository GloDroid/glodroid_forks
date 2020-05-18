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

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.time.Duration;
import java.util.Set;

/**
 * Host side integration tests for apexd.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexdHostTest extends BaseHostJUnit4Test  {

    private static final String SHIM_APEX_PATH = "/system/apex/com.android.apex.cts.shim.apex";

    private final ModuleTestUtils mTestUtils = new ModuleTestUtils(this);

    @Test
    public void testOrphanedApexIsNotActivated() throws Exception {
        assumeTrue("Device does not support updating APEX", mTestUtils.isApexUpdateSupported());
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
            mTestUtils.waitForFileDeleted("/data/apex/active/apexd_test_v2.apex",
                    Duration.ofMinutes(3));
        } finally {
            getDevice().executeShellV2Command("rm /data/apex/active/apexd_test_v2.apex");
        }
    }
    @Test
    public void testApexWithoutPbIsNotActivated() throws Exception {
        assumeTrue("Device does not support updating APEX", mTestUtils.isApexUpdateSupported());
        assumeTrue("Device requires root", getDevice().isAdbRoot());
        final String testApexFile = "com.android.apex.cts.shim.v2_no_pb.apex";
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
            mTestUtils.waitForFileDeleted("/data/apex/active/" + testApexFile,
                    Duration.ofMinutes(3));
        } finally {
            getDevice().executeShellV2Command("rm /data/apex/active/" + testApexFile);
        }
    }

    @Test
    public void testRemountApex() throws Exception {
        assumeTrue("Device does not support updating APEX", mTestUtils.isApexUpdateSupported());
        assumeTrue("Device requires root", getDevice().isAdbRoot());
        final File oldFile = getDevice().pullFile(SHIM_APEX_PATH);
        try {
            getDevice().remountSystemWritable();
            // In case remount requires a reboot, wait for boot to complete.
            getDevice().waitForBootComplete(Duration.ofMinutes(3).toMillis());
            final File newFile = mTestUtils.getTestFile("com.android.apex.cts.shim.v2.apex");
            // Stop framework
            getDevice().executeShellV2Command("stop");
            // Push new shim APEX. This simulates adb sync.
            getDevice().pushFile(newFile, SHIM_APEX_PATH);
            // Ask apexd to remount packages
            getDevice().executeShellV2Command("cmd -w apexservice remountPackages");
            // Start framework
            getDevice().executeShellV2Command("start");
            // Give enough time for system_server to boot.
            Thread.sleep(Duration.ofSeconds(15).toMillis());
            final Set<ITestDevice.ApexInfo> activeApexes = getDevice().getActiveApexes();
            ITestDevice.ApexInfo testApex = new ITestDevice.ApexInfo(
                    "com.android.apex.cts.shim", 2L);
            assertThat(activeApexes).contains(testApex);
        } finally {
            getDevice().pushFile(oldFile, SHIM_APEX_PATH);
            getDevice().reboot();
        }
    }

    @Test
    public void testApexWithoutPbIsNotActivated_ProductPartitionHasOlderVersion()
            throws Exception {
        assumeTrue("Device does not support updating APEX", mTestUtils.isApexUpdateSupported());
        assumeTrue("Device requires root", getDevice().isAdbRoot());

        withTestFiles(new String[]{
                "apex.apexd_test.apex", "/product/apex",
                "apex.apexd_test_v2_no_pb.apex", "/data/apex/active"
        }, () -> {
                final Set<ITestDevice.ApexInfo> activeApexes = getDevice().getActiveApexes();
                assertThat(activeApexes).contains(new ITestDevice.ApexInfo(
                        "com.android.apex.test_package", 1L));
                assertThat(activeApexes).doesNotContain(new ITestDevice.ApexInfo(
                        "com.android.apex.test_package", 2L));

                // v2_no_pb should be deleted
                mTestUtils.waitForFileDeleted("/data/apex/active/apex.apexd_test_v2_no_pb.apex",
                        Duration.ofMinutes(3));
            });
    }

    @Test
    public void testApexWithoutPbIsNotActivated_ProductPartitionHasNewerVersion()
            throws Exception {
        assumeTrue("Device does not support updating APEX", mTestUtils.isApexUpdateSupported());
        assumeTrue("Device requires root", getDevice().isAdbRoot());

        withTestFiles(new String[]{
                "apex.apexd_test_v3.apex", "/product/apex",
                "apex.apexd_test_v2_no_pb.apex", "/data/apex/active"
        }, () -> {
                final Set<ITestDevice.ApexInfo> activeApexes = getDevice().getActiveApexes();
                assertThat(activeApexes).contains(new ITestDevice.ApexInfo(
                        "com.android.apex.test_package", 3L));
                assertThat(activeApexes).doesNotContain(new ITestDevice.ApexInfo(
                        "com.android.apex.test_package", 2L));

                // v2_no_pb should be deleted
                mTestUtils.waitForFileDeleted("/data/apex/active/apex.apexd_test_v2_no_pb.apex",
                        Duration.ofMinutes(3));
            });
    }

    private interface ThrowingRunnable {
        void run() throws Exception;
    }

    private void withTestFiles(String[] files, ThrowingRunnable body) throws Exception {
        Assert.assertTrue("files should have even elements", files.length % 2 == 0);
        try {
            getDevice().remountSystemWritable();
            // In case remount requires a reboot, wait for boot to complete.
            assertWithMessage("Timed out waiting for device to boot").that(
                    getDevice().waitForBootComplete(Duration.ofMinutes(2).toMillis())).isTrue();

            // copy test files
            for (int i = 0; i < files.length; i += 2) {
                final String filename = files[i];
                final String path = files[i + 1];
                final File file = mTestUtils.getTestFile(filename);
                getDevice().pushFile(file,  path + "/" + filename);
            }

            getDevice().reboot();
            assertWithMessage("Timed out waiting for device to boot").that(
                    getDevice().waitForBootComplete(Duration.ofMinutes(2).toMillis())).isTrue();

            body.run();
        } finally {
            getDevice().remountSystemWritable();
            assertWithMessage("Timed out waiting for device to boot").that(
                    getDevice().waitForBootComplete(Duration.ofMinutes(2).toMillis())).isTrue();

            // remove test files
            for (int i = 0; i < files.length; i += 2) {
                final String filename = files[i];
                final String path = files[i + 1];
                getDevice().executeShellV2Command("rm " + path + "/" + filename);
            }
        }
    }
}
