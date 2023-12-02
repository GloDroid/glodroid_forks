src_minigbm_gralloc = files(
    'cros_gralloc_buffer.cc',
    'cros_gralloc_driver.cc',
    'cros_gralloc_helpers.cc',
)

libminigbm_gralloc = shared_library(
    'minigbm_gralloc_gd',
    src_minigbm_gralloc,
    link_whole: libminigbm,
    cpp_args: '-DHAS_DMABUF_SYSTEM_HEAP',
    install : true,
)

subdir('gralloc0')
subdir('gralloc4')
