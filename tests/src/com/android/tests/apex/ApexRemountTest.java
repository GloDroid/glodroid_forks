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

import com.android.apex.Protos.ApexManifest;
import com.android.tests.util.ModuleTestUtils;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.util.JsonFormat;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.util.concurrent.TimeUnit;

/**
 * Tests for automatic remount of APEXes when they are updated via `adb sync`
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class ApexRemountTest extends BaseHostJUnit4Test {
    private File mSavedShimFile;
    private final ModuleTestUtils mUtils = new ModuleTestUtils(this);

    private static final String SHIM_APEX_PATH = "/system/apex/com.android.apex.cts.shim.apex";

    @Before
    public void setUp() throws Exception {
        mUtils.abandonActiveStagedSession();
        mUtils.uninstallShimApexIfNecessary();
        mSavedShimFile = getDevice().pullFile(SHIM_APEX_PATH);
    }

    @After
    public void tearDown() throws Exception {
        if (mSavedShimFile != null) {
            getDevice().remountSystemWritable();
            getDevice().pushFile(mSavedShimFile, SHIM_APEX_PATH);
            getDevice().reboot();
            assertThat(getShimApexManifest().getVersion()).isEqualTo(1L);
        }
    }

    @Test
    public void testApexIsRemountedUponUpdate() throws Exception {
        assumeTrue("APEXes on the device are flattened", hasNonFlattenedApex());

        ModuleTestUtils utils = new ModuleTestUtils(this);
        File updatedFile = utils.getTestFile("com.android.apex.cts.shim.v2.apex");

        getDevice().remountSystemWritable();
        getDevice().pushFile(updatedFile, SHIM_APEX_PATH);

        // Wait some time until the update is detected by apexd and remount is done
        TimeUnit.SECONDS.sleep(5);

        assertThat(getShimApexManifest().getVersion()).isEqualTo(2L);
    }

    private ApexManifest getShimApexManifest() throws DeviceNotAvailableException,
            InvalidProtocolBufferException {
        String json = getDevice().executeShellCommand(
                "cat /apex/com.android.apex.cts.shim/apex_manifest.pb");
        ApexManifest.Builder builder = ApexManifest.newBuilder();
        JsonFormat.parser().merge(json, builder);
        return builder.build();
    }

    private boolean hasNonFlattenedApex() throws Exception {
        return "true".equals(getDevice().getProperty("ro.apex.updatable"));
    }
}
