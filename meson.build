project('file-monitor', 'c')
project_name = 'io.github.trbjo.vala-libraries.file-monitor'

inc = include_directories('src')

file_monitor_lib = static_library('file-monitor',
    sources: ['src/file-monitor.c'],
    include_directories: inc,
    install: false,
    c_args: ['-D_GNU_SOURCE', '-O3', '-fno-plt'],
)

vapi_dir = meson.current_source_dir()

file_monitor_dep = declare_dependency(
    link_with: file_monitor_lib,
    include_directories: [inc, include_directories('.')],
    variables: {'vapi_dir': vapi_dir}
)
