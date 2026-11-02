# Copyright (C) 2026 DarkMatter Productions
# Preserve the upstream version header template and its complete notice.
version_info = configuration_data()
version_info.set('major', 16)
version_info.set('minor', 6)
version_info.set('patch', 0)
version_info.set('flavor', '')
glslang_build_info = configure_file(input: '../build_info.h.tmpl',
  output: 'build_info.h', configuration: version_info)
