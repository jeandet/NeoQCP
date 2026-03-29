multigraph_perf = executable('multigraph_perf',
  'multigraph-perf.cpp',
  dependencies: [NeoQCP_dep],
  cpp_args: ['-DQT_WIDGETS_LIB', '-DQT_GUI_LIB'],
)

configure_file(
  input: 'run-perf.py',
  output: 'run-perf.py',
  copy: true,
)
