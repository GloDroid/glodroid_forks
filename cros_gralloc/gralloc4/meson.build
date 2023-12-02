src_allocator4 = files(
    'CrosGralloc4Allocator.cc',
    'CrosGralloc4AllocatorService.cc',
    'CrosGralloc4Utils.cc',
)

src_mapper4 = files(
    'CrosGralloc4Mapper.cc',
    'CrosGralloc4Utils.cc',
)

shared_library(
    'android.hardware.graphics.mapper@4.0-impl.minigbm_gd',
    src_mapper4,
    link_with : libminigbm_gralloc,
    name_prefix : '',
    dependencies : deps,
    include_directories: inc_include,
    install : true,
    install_dir : get_option('libdir') / 'hw',
)

executable(
    'android.hardware.graphics.allocator@4.0-service.minigbm_gd',
    src_allocator4,
    link_with : libminigbm_gralloc,
    dependencies : deps,
    include_directories: inc_include,
    install : true,
    install_dir : get_option('bindir') / 'hw',
)

configure_file(
    input: 'android.hardware.graphics.allocator@4.0-service.minigbm.rc',
    output: '@PLAINNAME@',
    copy: true,
    install_dir: get_option('sysconfdir') / 'init',
)
