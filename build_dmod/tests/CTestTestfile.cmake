# CMake generated Testfile for 
# Source directory: /home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests
# Build directory: /home/runner/work/dmffs/dmffs/build_dmod/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(tests_dmod_ctx "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_ctx")
set_tests_properties(tests_dmod_ctx PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_hlp "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_hlp")
set_tests_properties(tests_dmod_hlp PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_ldr "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_ldr")
set_tests_properties(tests_dmod_ldr PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_mgr "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_mgr")
set_tests_properties(tests_dmod_mgr PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_dmfc_api "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_dmfc_api")
set_tests_properties(tests_dmod_dmfc_api PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_getname "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_getname")
set_tests_properties(tests_dmod_getname PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_rawmem "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_rawmem")
set_tests_properties(tests_dmod_rawmem PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_mock_memory "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_mock_memory")
set_tests_properties(tests_dmod_mock_memory PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
add_test(tests_dmod_dir "/home/runner/work/dmffs/dmffs/build_dmod/tests/tests_dmod_dir")
set_tests_properties(tests_dmod_dir PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;40;add_test;/home/runner/work/dmffs/dmffs/build/_deps/dmod-src/tests/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
subdirs("mocks")
subdirs("common")
subdirs("module")
subdirs("system")
