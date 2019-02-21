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

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.IOException;

/**
 * Test to check if Apex can be staged, activated and uninstalled successfully.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class MediaSwCodecHostTest extends ApexE2EBaseHostTest {
    /**
     * Tests that if Apex package can be staged, activated and uninstalled successfully.
     */
    @Test
    public void testStageActivateUninstallApexPackage()
                                throws DeviceNotAvailableException, IOException {
        // Run tests only when the device has swcodec apex installed.
        for (ApexInfo info : getDevice().getActiveApexes()) {
            if (info.name.equals("com.android.media.swcodec")) {
                doTestStageActivateUninstallApexPackage();
                return;
            }
        }
        CLog.i("The device does not have swcodec apex installed. Skipping the test.");
    }

    @Override
    public void additionalCheck() {
        // TODO: Add the additional check for media module.
    }

}
