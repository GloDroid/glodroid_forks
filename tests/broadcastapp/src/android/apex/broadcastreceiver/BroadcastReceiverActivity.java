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

package android.apex.broadcastreceiver;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Bundle;
import android.util.Log;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * A simple activity to receive and log broadcasts.
 *
 */
public class BroadcastReceiverActivity extends BroadcastReceiver {
    private static final String TAG = "BroadcastReceiver";
    @Override
    public void onReceive(Context context, Intent intent) {
        StringBuilder sb = new StringBuilder();
        sb.append("Action: " + intent.getAction() + "\n");
        sb.append("URI: " + intent.toUri(Intent.URI_INTENT_SCHEME).toString() + "\n");
        String log = sb.toString();
        Log.d(TAG, log);
        Bundle extras = intent.getExtras();
        Log.d(TAG, extras.toString());

        PackageInstaller.SessionInfo p = intent.getParcelableExtra(PackageInstaller.EXTRA_SESSION);
        Log.d(TAG, "Session Info: " + p.toString());
        try {
            Method m = PackageInstaller.SessionInfo.class.getMethod("isSessionReady");
            boolean isSessionReady = (boolean) m.invoke(p);
            Log.d(TAG, "Session isSessionReady = " + isSessionReady);
            m = PackageInstaller.SessionInfo.class.getMethod("isSessionFailed");
            boolean isSessionFailed = (boolean) m.invoke(p);
            Log.d(TAG, "Session isSessionFailed = " + isSessionFailed);
            m = PackageInstaller.SessionInfo.class.getMethod("isSessionApplied");
            boolean isSessionApplied = (boolean) m.invoke(p);
            Log.d(TAG, "Session isSessionApplied = " + isSessionApplied);

        } catch (NoSuchMethodException | IllegalAccessException | InvocationTargetException e) {
            e.printStackTrace();
        }
    }
}
