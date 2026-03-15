
interactions_moc_files = qtmod.preprocess(moc_headers : ['mainwindow.h'], ui_files: ['mainwindow.ui'], dependencies: [NeoQCP_dep])

interactions = executable('interactions',
  'mainwindow.cpp',
  'main.cpp',
  interactions_moc_files,
  dependencies: [NeoQCP_dep]
)
