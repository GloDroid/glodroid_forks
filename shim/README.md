# Cts Shim Apex

## Overview

A shim apex is an apex that doesn't provide any functionality as it's sole
purpose is to enable CTS testing of APEX-related APIs.

A cts shim apex has following restrictions:

*   Its name is equal to `com.android.apex.cts.shim`
*   It is signed with a throw-away key.
*   Its `apex_payload.img` contains a single text file called `hash.txt` with a
    SHA512 hash of the next accepted version.
*   In the case of the final version (i.e. the one that doesn't accept further
    updates), it's `hash.txt` contains SHA512 hash of `/dev/null`.


## Building shim apexes

Modules to build shim apexes are defined in the
system/apex/shim/build/Android.bp blueprint file. For the purpose of CTS testing
only `com.android.apex.cts.shim.v1`, `com.android.apex.cts.shim.v2` and
`com.android.apex.cts.shim.v2_wrong_sha` are used. All other shim apexes are used
only for unit-testing purposes of the shim verification logic.

To build shim apexes simply run

```
m com.android.apex.cts.shim.v2 com.android.apex.cts.shim.v1 com.android.apex.cts.shim.v2_wrong_sha
```

Generated apexes will be located in the following folders.

*   out/target/product/${DEVICE}/obj/ETC/com.android.apex.cts.shim.v1_intermediates/com.android.apex.cts.shim.v1.apex
*   out/target/product/${DEVICE}/obj/ETC/com.android.apex.cts.shim.v2_intermediates/com.android.apex.cts.shim.v2.apex
*   out/target/product/${DEVICE}/obj/ETC/com.android.apex.cts.shim.v2_wrong_sha_intermediates/com.android.apex.cts.shim.v2_wrong_sha.apex

## Updating shim apexes

TODO(ioffe): fill

