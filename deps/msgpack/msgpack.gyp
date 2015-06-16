{
  'targets': [
    {
      'target_name': 'msgpack',
      'type': 'static_library',
      'include_dirs=': ['include'],
      'sources': [
        'src/unpack.c',
        'src/objectc.c',
        'src/version.c',
        'src/vrefbuffer.c',
        'src/zone.c',
      ],
      'ldflags': ['-version-info', '2:0:0', '-no-undefined'],
      'direct_dependent_settings': {
        'include_dirs': ['include'],
      }
    }
  ]
}

