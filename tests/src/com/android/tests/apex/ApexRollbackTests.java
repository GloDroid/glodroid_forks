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

import static org.junit.Assume.assumeTrue;

import com.android.tests.util.ModuleTestUtils;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.util.Set;

/**
 * Test for automatic recovery of apex update that causes boot loop.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexRollbackTests extends BaseHostJUnit4Test {
    private final ModuleTestUtils mUtils = new ModuleTestUtils(this);
    /**
     * Uninstalls any version greater than 1 of shim apex and reboots the device if necessary
     * to complete the uninstall.
     */
    @After
    public void tearDown() throws Exception {
        mUtils.abandonActiveStagedSession();
        mUtils.uninstallShimApexIfNecessary();
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
        device.setProperty("persist.debug.trigger_watchdog.apex", "com.android.apex.cts.shim@2");
        String error = device.installPackage(apexFile, false, "--wait");
        assertThat(error).isNull();

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
    }
}
