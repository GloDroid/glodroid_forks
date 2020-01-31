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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.IManagedTestDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.util.CommandResult;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.IOException;

/**
 * Test to check if Apex can be staged, activated and uninstalled successfully.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApiExtensionsHostTest extends ApexE2EBaseHostTest {

    @Test
    public void testDefault() throws Exception {
        CLog.i("Verifying default version");
        assertEquals(0, getExtensionVersionR());
    }

    @Override
    public void additionalCheck() throws DeviceNotAvailableException, IOException {
        CLog.i("Reading version");
        int version = getExtensionVersionR();
        assertEquals(45, version);

        CLog.i("Waiting for boot complete");
        assertTrue(((IManagedTestDevice) getDevice()).getMonitor().waitForBootComplete(60000));
    }

    private int getExtensionVersionR() throws DeviceNotAvailableException, IOException {
        CommandResult commandResult =
                getDevice().executeShellV2Command("getprop build.version.extensions.r");
        assertEquals(0, (int) commandResult.getExitCode());

        String outputString = commandResult.getStdout().replace("\n", "");
        return Integer.parseInt(outputString);
    }
}
