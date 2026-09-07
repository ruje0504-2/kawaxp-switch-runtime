project('kawaxp-compatibility-probe', 'c', default_options: ['c_std=c11'])
png = dependency('libpng', static: true)
lib = subproject('libai5')
deps = [lib.get_variable('libai5_dep'), png]
sources = ['tools/probe.c']
if host_machine.system() == 'horizon'
  sources += 'tools/switch_main.c'
endif
executable('kawaxp-probe', sources, dependencies: deps)
if get_option('runtime')
  static_deps = host_machine.system() == 'horizon'
  if host_machine.system() == 'horizon'
    runtime_src = ['runtime/vm.c','runtime/frontend.c','runtime/audio.c','runtime/text_ft.c']
    runtime_deps = deps + [dependency('sdl2', static: true), dependency('freetype2', static: true),
                           dependency('vorbisfile', static: true), dependency('vorbis', static: true),
                           dependency('ogg', static: true), dependency('zlib', static: true)]
  else
    runtime_src = ['runtime/vm.c','runtime/frontend.c','runtime/audio.c','runtime/text_ttf.c']
    runtime_deps = deps + [dependency('sdl2'), dependency('SDL2_ttf'), dependency('sndfile'), dependency('zlib')]
  endif
  runtime_src += ['runtime/ax.c','runtime/ax_render.c']
  executable('kawaxp', runtime_src, dependencies: runtime_deps, c_args: ['-D_DEFAULT_SOURCE'])
  if host_machine.system() != 'horizon'
    ax_test = executable('ax-test', ['tests/ax_test.c','runtime/ax.c','runtime/ax_render.c'],
                         dependencies: deps + [dependency('sdl2')], c_args: ['-D_DEFAULT_SOURCE'])
    test('AX playback and pixels', ax_test)
  endif
endif
