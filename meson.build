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

libwireplumber = dependency('wireplumber-0.4')
fmt = dependency('fmt', version : ['>=8.1.1'], fallback : ['fmt', 'fmt_dep'])
spdlog = dependency('spdlog', version : ['>=1.10.0'], fallback : ['spdlog', 'spdlog_dep'], default_options : ['external_fmt=enabled'])

src_files = files(
    'wp_notifyd.cpp'
)

exe = executable(
    'wp_notifyd',
    src_files,
    dependencies: [
        libwireplumber,
        spdlog
    ],
    install : true
)

test('basic', exe)
