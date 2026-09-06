cmake_minimum_required(VERSION 3.20)
project(PT7MP3
    VERSION 1.0.0
    DESCRIPTION "PT7-MP3 standalone MP3 Layer III decoder library"
    LANGUAGES CXX
)

# C++20 standard requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static library target
add_library(PT7MP3 STATIC
    include/pt7_mp3.h
    src/mp3_bitstream.h
    src/mp3_bitstream.cpp
    src/mp3_frame.h
    src/mp3_frame.cpp
    src/mp3_side_info.h
    src/mp3_side_info.cpp
    src/mp3_scalefactors.h
    src/mp3_scalefactors.cpp
    src/mp3_huffman_tables.h
    src/mp3_huffman.h
    src/mp3_huffman.cpp
    src/mp3_reservoir.h
    src/mp3_reservoir.cpp
    src/mp3_requant.h
    src/mp3_requant.cpp
    src/mp3_stereo.h
    src/mp3_stereo.cpp
    src/mp3_antialias.h
    src/mp3_antialias.cpp
    src/mp3_imdct.h
    src/mp3_imdct.cpp
    src/mp3_overlap.h
    src/mp3_overlap.cpp
    src/mp3_synth.h
    src/mp3_synth.cpp
    src/mp3_id3.h
    src/mp3_id3.cpp
    src/mp3_vbr.h
    src/mp3_vbr.cpp
    src/mp3_decoder.cpp
)

# Alias target PT7MP3::PT7MP3 for in-tree consumers
add_library(PT7MP3::PT7MP3 ALIAS PT7MP3)

# Include directories
target_include_directories(PT7MP3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Strict warning flags: 0 warnings policy
if(MSVC)
    target_compile_options(PT7MP3 PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(PT7MP3 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Standalone example executable (MP3 -> PCM16 WAV)
add_executable(decode_file
    examples/decode_file/main.cpp
)
target_link_libraries(decode_file PRIVATE PT7MP3::PT7MP3)

if(MSVC)
    target_compile_options(decode_file PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(decode_file PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Seek demo executable (MP3 -> seek -> PCM16 WAV)
add_executable(seek_demo
    examples/seek_demo/main.cpp
)
target_link_libraries(seek_demo PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(seek_demo PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(seek_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Minimal Windows GUI Player
if(WIN32)
    add_executable(pt7_mp3_player
        examples/minimal-player/main.cpp
        examples/minimal-player/player_core.cpp
        examples/minimal-player/wasapi_sink.cpp
    )
    target_include_directories(pt7_mp3_player PRIVATE examples/minimal-player)
    target_link_libraries(pt7_mp3_player PRIVATE
        PT7MP3::PT7MP3
        ole32
        comctl32
        comdlg32
        ksuser
        avrt
    )
    if(MSVC)
        target_compile_options(pt7_mp3_player PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(pt7_mp3_player PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

# Tests
enable_testing()

# API test (public C API only)
add_executable(test_api
    tests/test_api.cpp
)
target_link_libraries(test_api PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(test_api PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_api PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_api COMMAND test_api)
set_tests_properties(test_api PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Robustness test (Phase B)
add_executable(test_robustness
    tests/test_robustness.cpp
)
target_link_libraries(test_robustness PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(test_robustness PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_robustness PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_robustness COMMAND test_robustness)
set_tests_properties(test_robustness PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Metadata + Gapless test (Phase C)
add_executable(test_metadata
    tests/test_metadata.cpp
)
target_link_libraries(test_metadata PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(test_metadata PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_metadata PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_metadata COMMAND test_metadata)
set_tests_properties(test_metadata PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Seeking test (Phase D)
add_executable(test_seek
    tests/test_seek.cpp
)
target_link_libraries(test_seek PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(test_seek PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_seek PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_seek COMMAND test_seek)
set_tests_properties(test_seek PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Benchmark (Phase E) — not in ctest, run manually
add_executable(test_benchmark
    tests/test_benchmark.cpp
)
target_link_libraries(test_benchmark PRIVATE PT7MP3::PT7MP3)
if(WIN32)
    target_link_libraries(test_benchmark PRIVATE psapi)
endif()
if(MSVC)
    target_compile_options(test_benchmark PRIVATE /W4 /WX /permissive-)
    target_link_options(test_benchmark PRIVATE /SUBSYSTEM:CONSOLE)
else()
    target_compile_options(test_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Task 1 Foundation test
add_executable(test_foundation
    tests/test_foundation.cpp
)
target_link_libraries(test_foundation PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(test_foundation PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_foundation PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_foundation COMMAND test_foundation)

# Task 2 Side Information & Huffman test
add_executable(test_task2
    tests/test_task2.cpp
)
target_link_libraries(test_task2 PRIVATE PT7MP3::PT7MP3)
target_compile_definitions(test_task2 PRIVATE TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors")
if(MSVC)
    target_compile_options(test_task2 PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_task2 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_task2 COMMAND test_task2)
set_tests_properties(test_task2 PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Task 5 Compatibility Matrix test
add_executable(test_compatibility
    tests/test_compatibility.cpp
)
target_link_libraries(test_compatibility PRIVATE PT7MP3::PT7MP3)
target_include_directories(test_compatibility PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(test_compatibility PRIVATE TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors")
if(MSVC)
    target_compile_options(test_compatibility PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_compatibility PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_compatibility COMMAND test_compatibility)
set_tests_properties(test_compatibility PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Task 5 Fuzz test
add_executable(test_fuzz
    tests/test_fuzz.cpp
)
target_link_libraries(test_fuzz PRIVATE PT7MP3::PT7MP3)
if(MSVC)
    target_compile_options(test_fuzz PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_fuzz PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_fuzz COMMAND test_fuzz 1000)

# Task 5 Stress test
add_executable(test_stress
    tests/test_stress.cpp
)
target_link_libraries(test_stress PRIVATE PT7MP3::PT7MP3)
target_compile_definitions(test_stress PRIVATE TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors")
if(MSVC)
    target_compile_options(test_stress PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(test_stress PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_stress COMMAND test_stress)
set_tests_properties(test_stress PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# CMake Installation & Packaging
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# License metadata
set(PT7MP3_LICENSE "MIT" CACHE STRING "PT7-MP3 license type")
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(TARGETS PT7MP3
    EXPORT PT7MP3Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES include/pt7_mp3.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT PT7MP3Targets
    FILE PT7MP3Targets.cmake
    NAMESPACE PT7MP3::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PT7MP3
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PT7MP3ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PT7MP3Config.cmake"
    "include(CMakeFindDependencyMacro)\n"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/PT7MP3Targets.cmake\")\n"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PT7MP3Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PT7MP3ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PT7MP3
)
