{
  'variables': {
    'uv_library%': 'static_library',
  },
  'target_defaults': {
    'cflags': [
      '-std=c99',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-Wconversion',
      '-Wextra',
      '-Wstrict-prototypes',
      '-pedantic',
    ],
    'defines': [
      'DAEMON_MODULE_INSTALL_PATH="<(module_root_dir)/daemon.js"',
      'WORKER_MODULE_INSTALL_PATH="<(module_root_dir)/worker.js"',
      'UNIX_TEMPDIR="/tmp"', # TODO this must be detected properly.
    ],
    'dependencies': [
      '<(module_root_dir)/deps/libuv/uv.gyp:libuv',
      '<(module_root_dir)/deps/msgpack/msgpack.gyp:msgpack',
    ],
  },
  'targets': [
    {
      'target_name': 'libdaemonify',
      'type': 'static_library',
      'sources': [
        'src/daemonify.c',
        'src/util.c',
      ],
      # must reset or libuv includes from node-gyp will be prepended
      'include_dirs=': [],
    },
    {
      'target_name': 'daemonify',
      'type': 'executable',
      'sources': [
        'src/main.c',
      ],
      'dependencies': [
        'libdaemonify',
      ],
      # must reset or libuv includes from node-gyp will be prepended
      'include_dirs=': [],
    },
  ]
}
