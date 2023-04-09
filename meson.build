project(
    'ffmpeg_codec2',
    ['c', 'cpp'],
    version : '2',
    license : 'APACHE-2.0',
    meson_version : '>= 0.56',
    default_options : ['buildtype=debugoptimized', 'b_ndebug=if-release', 'c_std=c11', 'cpp_std=c++17', 'cpp_rtti=false']
)

inc_dir = [include_directories('.')]

src = files(
    'C2FFMPEGAudioDecodeComponent.cpp',
    'C2FFMPEGAudioDecodeInterface.cpp',
    'C2FFMPEGVideoDecodeComponent.cpp',
    'C2FFMPEGVideoDecodeInterface.cpp',
    'service.cpp',
)

deps = [
]

cpp_args = [
    '-Wno-inconsistent-missing-override',
]

subdir('ffmpeg_utils')

executable(
    'android.hardware.media.c2@1.2-service-ffmpeg',
    src,
    cpp_args : cpp_args,
    dependencies : deps,
    install : true,
    install_dir : get_option('bindir') / 'hw',
    include_directories: inc_dir,
)

configure_file(
    input: 'android.hardware.media.c2@1.2-service-ffmpeg.rc',
    output: '@PLAINNAME@',
    copy: true,
    install_dir: get_option('sysconfdir') / 'init',
)

configure_file(
    input: 'android.hardware.media.c2@1.2-service-ffmpeg.xml',
    output: '@PLAINNAME@',
    copy: true,
    install_dir: get_option('sysconfdir') / 'vintf' / 'manifest',
)

configure_file(
    input: 'media_codecs_ffmpeg_c2.xml',
    output: '@PLAINNAME@',
    copy: true,
    install_dir: get_option('sysconfdir'),
)
