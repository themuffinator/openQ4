# Copyright (C) 2026 DarkMatter Productions
# Native Meson overlay for the pinned glslang 16.6.0 compiler. The source
# selection follows upstream glslang/CMakeLists.txt and SPIRV/CMakeLists.txt;
# no upstream sources or notices are changed. See README credits and LICENSE.txt.
project('glslang', 'cpp', version: '16.6.0', default_options: ['cpp_std=c++17'])

subdir('glslang')
glslang_sources = files(
  'glslang/CInterface/glslang_c_interface.cpp',
  'glslang/GenericCodeGen/CodeGen.cpp',
  'glslang/GenericCodeGen/Link.cpp',
  'glslang/MachineIndependent/glslang_tab.cpp',
  'glslang/MachineIndependent/attribute.cpp',
  'glslang/MachineIndependent/Constant.cpp',
  'glslang/MachineIndependent/iomapper.cpp',
  'glslang/MachineIndependent/InfoSink.cpp',
  'glslang/MachineIndependent/Initialize.cpp',
  'glslang/MachineIndependent/IntermTraverse.cpp',
  'glslang/MachineIndependent/Intermediate.cpp',
  'glslang/MachineIndependent/ParseContextBase.cpp',
  'glslang/MachineIndependent/ParseHelper.cpp',
  'glslang/MachineIndependent/PoolAlloc.cpp',
  'glslang/MachineIndependent/RemoveTree.cpp',
  'glslang/MachineIndependent/Scan.cpp',
  'glslang/MachineIndependent/ShaderLang.cpp',
  'glslang/MachineIndependent/SpirvIntrinsics.cpp',
  'glslang/MachineIndependent/SymbolTable.cpp',
  'glslang/MachineIndependent/Versions.cpp',
  'glslang/MachineIndependent/intermOut.cpp',
  'glslang/MachineIndependent/limits.cpp',
  'glslang/MachineIndependent/linkValidate.cpp',
  'glslang/MachineIndependent/parseConst.cpp',
  'glslang/MachineIndependent/reflection.cpp',
  'glslang/MachineIndependent/preprocessor/Pp.cpp',
  'glslang/MachineIndependent/preprocessor/PpAtom.cpp',
  'glslang/MachineIndependent/preprocessor/PpContext.cpp',
  'glslang/MachineIndependent/preprocessor/PpScanner.cpp',
  'glslang/MachineIndependent/preprocessor/PpTokens.cpp',
  'glslang/MachineIndependent/propagateNoContraction.cpp',
  'glslang/ResourceLimits/ResourceLimits.cpp',
  'glslang/ResourceLimits/resource_limits_c.cpp',
  'SPIRV/GlslangToSpv.cpp',
  'SPIRV/InReadableOrder.cpp',
  'SPIRV/Logger.cpp',
  'SPIRV/SpvBuilder.cpp',
  'SPIRV/SpvPostProcess.cpp',
  'SPIRV/doc.cpp',
  'SPIRV/SpvTools.cpp',
  'SPIRV/disassemble.cpp',
  'SPIRV/CInterface/spirv_c_interface.cpp',
)

glslang_arguments = ['-DENABLE_SPIRV', '-DENABLE_OPT=0']
if host_machine.system() == 'windows'
  glslang_sources += files('glslang/OSDependent/Windows/ossource.cpp')
  glslang_arguments += '-DGLSLANG_OSINCLUDE_WIN32'
else
  glslang_sources += files('glslang/OSDependent/Unix/ossource.cpp')
  glslang_arguments += '-DGLSLANG_OSINCLUDE_UNIX'
endif
glslang_threads = dependency('threads')
glslang_includes = include_directories('.')
glslang_library = static_library('glslang', glslang_sources, glslang_build_info,
  include_directories: glslang_includes,
  cpp_args: glslang_arguments,
  dependencies: glslang_threads,
  override_options: ['warning_level=0'],
  gnu_symbol_visibility: 'hidden',
  pic: true,
  install: false,
)
glslang_dep = declare_dependency(link_with: glslang_library,
  include_directories: glslang_includes, dependencies: glslang_threads)
