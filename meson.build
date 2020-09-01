project(
    'drm_hwcomposer',
    ['c', 'cpp'],
    version : '2',
    license : 'APACHE-2.0',
    meson_version : '>= 0.56',
    default_options : ['buildtype=debugoptimized', 'b_ndebug=if-release', 'c_std=c11', 'cpp_std=c++17', 'cpp_rtti=false']
)

srcs = [
    'hwc2_device/hwc2_device.cpp',
    'hwc2_device/DrmHwcTwo.cpp',
    'hwc2_device/HwcDisplayConfigs.cpp',
    'hwc2_device/HwcDisplay.cpp',
    'hwc2_device/HwcLayer.cpp',
    'compositor/DrmKmsPlan.cpp',
    'utils/Worker.cpp',
    'bufferinfo/BufferInfoMapperMetadata.cpp',
    'bufferinfo/BufferInfoYagi.cpp',
    'bufferinfo/legacy/BufferInfoLibdrm.cpp',
    'bufferinfo/BufferInfoGetter.cpp',
    'backend/BackendManager.cpp',
    'backend/Backend.cpp',
    'backend/BackendClient.cpp',
    'backend/BackendSunxi.cpp',
    'drm/DrmCrtc.cpp',
    'drm/DrmDisplayPipeline.cpp',
    'drm/VSyncWorker.cpp',
    'drm/UEventListener.cpp',
    'drm/DrmFbImporter.cpp',
    'drm/DrmDevice.cpp',
    'drm/DrmPlane.cpp',
    'drm/DrmProperty.cpp',
    'drm/DrmEncoder.cpp',
    'drm/ResourceManager.cpp',
    'drm/DrmMode.cpp',
    'drm/DrmConnector.cpp',
    'drm/DrmAtomicStateManager.cpp',
]

deps = [
    dependency('cutils'),
    dependency('drm'),
    dependency('hardware'),
    dependency('hidlbase'),
    dependency('log'),
    dependency('sync'),
    dependency('ui'),
    dependency('utils'),
]

cpp_flags = [
    '-DUSE_IMAPPER4_METADATA_API',
    '-DHWC2_INCLUDE_STRINGIFICATION',
    '-DHWC2_USE_CPP11',
]

shared_library(
    'hwcomposer.drm',
    srcs,
    name_prefix : '',
    cpp_args : cpp_flags,
    dependencies : deps,
    install : true,
    install_dir : get_option('libdir') / 'hw',
)

shared_library(
    'yagi_crosapi',
    'bufferinfo/yagi/yagi_crosapi.c',
    dependencies : deps,
    install : true,
)
