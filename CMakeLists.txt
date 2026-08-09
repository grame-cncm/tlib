cmake_minimum_required(VERSION 3.10)

project(tlib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wno-unused-parameter")

if (TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=-*,clang-*,performance-*,llvm-*,modernize-*,-modernize-use-trailing-return-type,cert-*")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# the library
add_library(tlib STATIC
    tlib/tlib.cpp
    tlib/tlib-error.cpp
    tlib/garbageable.cpp
    tlib/symbol.cpp
    tlib/node.cpp
    tlib/tree.cpp
    tlib/list.cpp
    tlib/recursive-tree.cpp
    tlib/recursive-print.cpp
    tlib/dcond.cpp)

# DirectedGraph/ is the first vendored dependency of tlib. Header-only, so there is
# nothing to compile or link -- only an include path. Recursive trees are part of
# tlib's identity, and so is the strongly-connected-component structure of a recursive
# term : rather than keep a hand-written Tarjan, tlib reuses the standalone library.
target_include_directories(tlib PUBLIC tlib DirectedGraph)

# the test executable
add_executable(tlib-tests main.cpp tests.cpp tests.hh)
target_link_libraries(tlib-tests PRIVATE tlib)
add_test(NAME tlib-tests COMMAND tlib-tests)

# the benchmark executable
add_executable(tlib-benchmark benchmark.cpp)
target_link_libraries(tlib-benchmark PRIVATE tlib)
# $<CONFIG> (not CMAKE_BUILD_TYPE) so this also works with multi-config
# generators (Xcode, Visual Studio) ; empty on a single-config generator
# with no build type set, which the benchmark reports as a visible warning.
target_compile_definitions(tlib-benchmark PRIVATE TLIB_BUILD_TYPE="$<CONFIG>")

# recursive tree conversion demo
add_executable(tlib-recursive-demo recursive-demo.cpp)
target_link_libraries(tlib-recursive-demo PRIVATE tlib)

# The demo and the benchmark carry validation checks (canonical conversion,
# rewrite roundtrips) : run them in ctest so a semantic change that breaks
# their narrative is caught, not silently shipped. --runs 1 keeps the
# benchmark smoke test fast ; timings are irrelevant here, only the notes.
add_test(NAME tlib-recursive-demo COMMAND tlib-recursive-demo)
add_test(NAME tlib-benchmark-smoke COMMAND tlib-benchmark --scale 1 --runs 1)

# The executable claims of A-GUIDED-TOUR-OF-TLIB.md. The guided tour quotes this
# file by line reference instead of carrying dead excerpts, so a documented
# behaviour that stops being true breaks a test rather than turning the document
# into fiction. Same reasoning as the demo and the benchmark above.
add_executable(tlib-tour-examples tour-examples.cpp)
target_link_libraries(tlib-tour-examples PRIVATE tlib)
add_test(NAME tlib-tour-examples COMMAND tlib-tour-examples)
