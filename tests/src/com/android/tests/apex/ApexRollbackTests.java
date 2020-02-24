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

package com.android.tests.apex;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assume.assumeTrue;

import com.android.tests.util.ModuleTestUtils;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.time.Duration;
import java.util.Set;
import java.util.concurrent.TimeUnit;

/**
 * Test for automatic recovery of apex update that causes boot loop.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexRollbackTests extends BaseHostJUnit4Test {
    private final ModuleTestUtils mUtils = new ModuleTestUtils(this);

    @Before
    public void setUp() throws Exception {
        mUtils.abandonActiveStagedSession();
        mUtils.uninstallShimApexIfNecessary();
        resetProperty("persist.debug.trigger_watchdog.apex");
        resetProperty("persist.debug.trigger_updatable_crashing_for_testing");
        resetProperty("persist.debug.trigger_reboot_after_activation");
    }

    /**
     * Uninstalls any version greater than 1 of shim apex and reboots the device if necessary
     * to complete the uninstall.
     */
    @After
    public void tearDown() throws Exception {
        mUtils.abandonActiveStagedSession();
        mUtils.uninstallShimApexIfNecessary();
        resetProperty("persist.debug.trigger_watchdog.apex");
        resetProperty("persist.debug.trigger_updatable_crashing_for_testing");
        resetProperty("persist.debug.trigger_reboot_after_activation");
    }

    private void resetProperty(String propertyName) throws Exception {
        assertWithMessage("Failed to reset value of property %s", propertyName).that(
                getDevice().setProperty(propertyName, "")).isTrue();
    }

    /**
     * Test for automatic recovery of apex update that causes boot loop.
     */
    @Test
    public void testAutomaticBootLoopRecovery() throws Exception {
        assumeTrue("Device does not support updating APEX", mUtils.isApexUpdateSupported());

        File apexFile = mUtils.getTestFile("com.android.apex.cts.shim.v2.apex");

        // To simulate an apex update that causes a boot loop, we install a
        // trigger_watchdog.rc file that arranges for a trigger_watchdog.sh
        // script to be run at boot. The trigger_watchdog.sh script checks if
        // the apex version specified in the property
        // persist.debug.trigger_watchdog.apex is installed. If so,
        // trigger_watchdog.sh repeatedly kills the system server causing a
        // boot loop.
        ITestDevice device = getDevice();
        assertThat(device.setProperty("persist.debug.trigger_watchdog.apex",
                "com.android.apex.cts.shim@2")).isTrue();
        String error = device.installPackage(apexFile, false, "--wait");
        assertThat(error).isNull();

        String sessionIdToCheck = device.executeShellCommand("pm get-stagedsessions --only-ready "
                + "--only-parent --only-sessionid").trim();
        assertThat(sessionIdToCheck).isNotEmpty();

        // After we reboot the device, we expect the device to go into boot
        // loop from trigger_watchdog.sh. Native watchdog should detect and
        // report the boot loop, causing apexd to roll back to the previous
        // version of the apex and force reboot. When the device comes up
        // after the forced reboot, trigger_watchdog.sh will see the different
        // version of the apex and refrain from forcing a boot loop, so the
        // device will be recovered.
        device.reboot();

        ApexInfo ctsShimV1 = new ApexInfo("com.android.apex.cts.shim", 1L);
        ApexInfo ctsShimV2 = new ApexInfo("com.android.apex.cts.shim", 2L);
        Set<ApexInfo> activatedApexes = device.getActiveApexes();
        assertThat(activatedApexes).contains(ctsShimV1);
        assertThat(activatedApexes).doesNotContain(ctsShimV2);

        // Assert that a session has failed with the expected reason
        String sessionInfo = device.executeShellCommand("cmd apexservice getStagedSessionInfo "
                    + sessionIdToCheck);
        assertThat(sessionInfo).contains("revertReason: zygote");
    }

    @Test
    public void testCheckpointingRevertsSession() throws Exception {
        assumeTrue("Device does not support updating APEX", mUtils.isApexUpdateSupported());
        assumeTrue("Device doesn't support fs checkpointing", supportsFsCheckpointing());

        File apexFile = mUtils.getTestFile("com.android.apex.cts.shim.v2.apex");

        ITestDevice device = getDevice();
        assertThat(device.setProperty("persist.debug.trigger_reboot_after_activation",
                "com.android.apex.cts.shim@2.apex")).isTrue();
        String error = device.installPackage(apexFile, false, "--wait");
        assertThat(error).isNull();

        String sessionIdToCheck = device.executeShellCommand("pm get-stagedsessions --only-ready "
                + "--only-parent --only-sessionid").trim();
        assertThat(sessionIdToCheck).isNotEmpty();

        // After we reboot the device, the apexd session should be activated as normal. After this,
        // trigger_reboot.sh will reboot the device before the system server boots. Checkpointing
        // will kick in, and at the next boot any non-finalized sessions will be reverted.
        device.reboot();

        ApexInfo ctsShimV1 = new ApexInfo("com.android.apex.cts.shim", 1L);
        ApexInfo ctsShimV2 = new ApexInfo("com.android.apex.cts.shim", 2L);
        Set<ApexInfo> activatedApexes = device.getActiveApexes();
        assertThat(activatedApexes).contains(ctsShimV1);
        assertThat(activatedApexes).doesNotContain(ctsShimV2);
    }

    // TODO(ioffe): check that we recover from the boot loop in of userspace reboot.

    /**
     * Test to verify that apexd won't boot loop a device in case {@code sys.init
     * .updatable_crashing} is {@code true} and there is no apex session to revert.
     */
    @Test
    public void testApexdDoesNotBootLoopDeviceIfThereIsNothingToRevert() throws Exception {
        assumeTrue("Device does not support updating APEX", mUtils.isApexUpdateSupported());
        // On next boot trigger setprop sys.init.updatable_crashing 1, which will trigger a
        // revert mechanism in apexd. Since there is nothing to revert, this should be a no-op
        // and device will boot successfully.
        assertThat(getDevice().setProperty("persist.debug.trigger_updatable_crashing_for_testing",
                "1")).isTrue();
        getDevice().reboot();
        assertWithMessage("Device didn't boot in 1 minute").that(
                getDevice().waitForBootComplete(Duration.ofMinutes(1).toMillis())).isTrue();
        // Verify that property was set to true.
        // TODO(b/149733368): Revert this workaround when the bug is fixed.
        // #getBooleanProperty fails due to timeout when the device is busy right after reboot.
        // Let's call #executeShellV2Command with 2 mins timeout to work around it.
        String val = getDevice().executeShellV2Command(
                "getprop sys.init.updatable_crashing", 2, TimeUnit.MINUTES).getStdout().trim();
        assertThat(val).isEqualTo("1");
    }

    /**
     * Test to verify that if a hard reboot is triggered far enough during userspace reboot boot
     * sequence, an apex update will be reverted.
     */
    @Test
    public void testFailingUserspaceReboot_revertsUpdate() throws Exception {
        assumeTrue("Device does not support updating APEX", mUtils.isApexUpdateSupported());
        assumeTrue("Device doesn't support usespace reboot",
                getDevice().getBooleanProperty("ro.init.userspace_reboot.is_supported", false));
        assumeTrue("Device doesn't support fs checkpointing", supportsFsCheckpointing());

        File apexFile = mUtils.getTestFile("com.android.apex.cts.shim.v2.apex");
        // Simulate failure in userspace reboot by triggering a full reboot in the middle of the
        // boot sequence.
        assertThat(getDevice().setProperty("test.apex_revert_test_force_reboot", "1")).isTrue();
        String error = getDevice().installPackage(apexFile, false, "--wait");
        assertWithMessage("Failed to stage com.android.apex.cts.shim.v2.apex : %s", error).that(
                error).isNull();
        // After we reboot the device, apexd will apply the update, but since device is rebooted
        // again later in the boot sequence update will be reverted.
        getDevice().rebootUserspace();
        // Verify that hard reboot happened.
        assertThat(getDevice().getIntProperty("sys.init.userspace_reboot.last_finished",
                -1)).isEqualTo(-1);
        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        assertThat(activatedApexes).contains(new ApexInfo("com.android.apex.cts.shim", 1L));
    }

    /**
     * Test to verify that if a hard reboot is triggered before executing init executes {@code
     * /system/bin/vdc checkpoint markBootAttempt} of userspace reboot boot sequence, apex update
     * still will be installed.
     */
    @Test
    public void testUserspaceRebootFailedShutdownSequence_doesNotRevertUpdate() throws Exception {
        assumeTrue("Device does not support updating APEX", mUtils.isApexUpdateSupported());
        assumeTrue("Device doesn't support usespace reboot",
                getDevice().getBooleanProperty("ro.init.userspace_reboot.is_supported", false));
        assumeTrue("Device doesn't support fs checkpointing", supportsFsCheckpointing());

        File apexFile = mUtils.getTestFile("com.android.apex.cts.shim.v2.apex");
        // Simulate failure in userspace reboot by triggering a full reboot in the middle of the
        // boot sequence.
        assertThat(getDevice().setProperty("test.apex_userspace_reboot_simulate_shutdown_failed",
                "1")).isTrue();
        String error = getDevice().installPackage(apexFile, false, "--wait");
        assertWithMessage("Failed to stage com.android.apex.cts.shim.v2.apex : %s", error).that(
                error).isNull();
        // After the userspace reboot started, we simulate it's failure by rebooting device during
        // on userspace-reboot-requested action. Since boot attempt hasn't been marked yet, next
        // boot will apply the update.
        assertThat(getDevice().getIntProperty("test.apex_userspace_reboot_simulate_shutdown_failed",
                0)).isEqualTo(1);
        getDevice().rebootUserspace();
        // Verify that hard reboot happened.
        assertThat(getDevice().getIntProperty("sys.init.userspace_reboot.last_finished",
                -1)).isEqualTo(-1);
        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        assertThat(activatedApexes).contains(new ApexInfo("com.android.apex.cts.shim", 2L));
    }

    /**
     * Test to verify that if a hard reboot is triggered around the time of
     * executing {@code /system/bin/vdc checkpoint markBootAttempt} of userspace reboot boot
     * sequence, apex update won't be installed.
     */
    @Test
    public void testUserspaceRebootFailedRemount_revertsUpdate() throws Exception {
        assumeTrue("Device does not support updating APEX", mUtils.isApexUpdateSupported());
        assumeTrue("Device doesn't support usespace reboot",
                getDevice().getBooleanProperty("ro.init.userspace_reboot.is_supported", false));
        assumeTrue("Device doesn't support fs checkpointing", supportsFsCheckpointing());

        File apexFile = mUtils.getTestFile("com.android.apex.cts.shim.v2.apex");
        // Simulate failure in userspace reboot by triggering a full reboot in the middle of the
        // boot sequence.
        assertThat(getDevice().setProperty("test.apex_userspace_reboot_simulate_remount_failed",
                "1")).isTrue();
        String error = getDevice().installPackage(apexFile, false, "--wait");
        assertWithMessage("Failed to stage com.android.apex.cts.shim.v2.apex : %s", error).that(
                error).isNull();
        // After we reboot the device, apexd will apply the update, but since device is rebooted
        // again later in the boot sequence update will be reverted.
        getDevice().rebootUserspace();
        // Verify that hard reboot happened.
        assertThat(getDevice().getIntProperty("sys.init.userspace_reboot.last_finished",
                -1)).isEqualTo(-1);
        Set<ApexInfo> activatedApexes = getDevice().getActiveApexes();
        assertThat(activatedApexes).contains(new ApexInfo("com.android.apex.cts.shim", 1L));
    }

    private boolean supportsFsCheckpointing() throws Exception {
        CommandResult result = getDevice().executeShellV2Command("sm supports-checkpoint");
        assertWithMessage("Failed to check if fs checkpointing is supported : %s",
                result.getStderr()).that(result.getStatus()).isEqualTo(CommandStatus.SUCCESS);
        return "true".equals(result.getStdout().trim());
    }
}
