project(
    'minigbm',
    ['c', 'cpp'],
    license : 'BSD?',
    meson_version : '>= 0.56',
    default_options : ['buildtype=debugoptimized', 'b_ndebug=if-release', 'c_std=c11', 'cpp_std=c++17', 'cpp_rtti=false']
)

inc_include = [include_directories('.')]

cpu_family = host_machine.cpu_family()

deps = [
    dependency('cutils'),
    dependency('drm'),
    dependency('hardware'),
    dependency('hidlbase'),
    dependency('log'),
]

src_x86 = files(
    'amdgpu.c',
    'i915.c',
)

src_arm = files(
    'rockchip.c',
    'msm.c',
    'vc4.c',
    'mediatek.c',
)

src_minigbm = files(
    'drv_array_helpers.c',
    'drv_helpers.c',
    'minigbm_helpers.c',
    'virtgpu.c',
    'virtgpu_virgl.c',
    'dri.c',
    'drv.c',
    'virtgpu_cross_domain.c',
    'dmabuf_driver/dmabuf_external_driver.cpp',
    'dmabuf_driver/dmabuf_internals.cpp',
)

c_flags = [
    '-DDRV_DMABUF_HEAP',
    '-DDRV_GBM_MESA',
]

if cpu_family == 'x86' or cpu_family == 'x86_64'
    src_minigbm += src_x86
    c_flags += [
        '-DDRV_AMDGPU',
        '-DDRV_I915',
        '-DDRV_RADEON',
    ]
elif cpu_family == 'arm' or cpu_family == 'aarch64'
    src_minigbm += src_arm
    c_flags += [
#        '-DDRV_MEDIATEK', # require custom libdrm
        '-DDRV_MESON',
        '-DDRV_MSM',
#        '-DDRV_ROCKCHIP', # require custom libdrm
#        '-DDRV_VC4', # vc4 looks verey dumb, gbm_mesa should work better
    ]
elif
    error('CPU family not supported')
endif

subdir('gbm_mesa_driver')

libminigbm = static_library(
    'minigbm',
    src_minigbm + src_gbm_mesa_driver,
    c_args : c_flags,
    cpp_args : c_flags,
    dependencies : deps,
)

subdir('cros_gralloc')
