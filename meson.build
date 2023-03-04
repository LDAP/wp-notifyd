project(
    'wp-notifyd',
    'cpp',
    version : '0.1',
    default_options : [
        'cpp_std=c++20',
        'buildtype=release',
        'default_library=static'
    ]
)

fmt = dependency('fmt', version : ['>=8.1.1'], fallback : ['fmt', 'fmt_dep'])
libnotify = dependency('libnotify')
libwireplumber = dependency('wireplumber-0.4')
spdlog = dependency('spdlog', version : ['>=1.10.0'], fallback : ['spdlog', 'spdlog_dep'], default_options : ['external_fmt=enabled'])

src_files = files(
    'wp_notifyd.cpp'
)

exe = executable(
    'wp-notifyd',
    src_files,
    dependencies: [
        fmt,
        libnotify,
        libwireplumber,
        spdlog
    ],
    install : true
)

test('basic', exe)
