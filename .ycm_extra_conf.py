import os

flags = []
cwd = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(cwd, 'binding.gyp')) as f:
    config = eval(f.read())['target_defaults']
    for flag in config['cflags']:
        flags.append(flag)
    for define in config['defines']:
        flags.append('-D%s' % define)
    for dep in config['dependencies']:
        flags.append('-isystem')
        flags.append(os.path.join(os.path.dirname(dep), 'include'))

flags = [flag.replace('<(module_root_dir)', cwd) for flag in flags]

def FlagsForFile(filename, **kwargs):
  return {
    'flags': flags,
    'do_cache': True
  }
