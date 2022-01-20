
## Feature

Support relaxed requirements for the most bottom plane

## Description:

1. Some of DRM/KMS drivers has no planes with formats which Android does require,
   but in some cases it's possible to modify the format of the buffer,
   making such planes usable without any drawbacks.

2. Another use-case is blend mode support. Android does require premultiplied blending mode support for all planes,
   but such requirement can be made optional for the most bottom plane without any drawbacks.

## Known use-cases:

### 1. sun4i/drm mainline driver kernel 5.4+

DE2.0 SUN4I-VI-0 plane has no format with alpha channel due to hardware limitations

|Layer|Plane# [int. name]|Buffer format|Resolved format|ZPOS|
|---|---|---|---|---|
|DEVICE|0 [SUN4I-UI-0]|DRM_FORMAT_ABGR8888| - |1|
|DEVICE|1 [SUN4I-VI-0]|DRM_FORMAT_ABGR8888|DRM_FORMAT_XBGR8888|0|
|DEVICE|2 [SUN4I-UI-1]|DRM_FORMAT_ABGR8888| - |2|
|DEVICE|3 [SUN4I-UI-2]|DRM_FORMAT_ABGR8888| - |3|

With this feature we are able to use SUN4I-VI-0 as most bottom plane (zpos=0)

## Test

1. Modify kernel driver and remove all alpha-enabled formats from drm/kms driver.
2. Ensure android boots with UI in CLIENT mode

Kernel must not support DRM_FORMAT_ABGR8888 after changes made.

|Layer|Plane|Buffer format|Resolved format|
|---|---|---|---|
|CLIENT|0|DRM_FORMAT_ABGR8888|DRM_FORMAT_XBGR8888|
