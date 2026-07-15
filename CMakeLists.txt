cmake_minimum_required(VERSION 3.20)

project(370obd2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GUI "Build the Qt desktop GUI" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(obd_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(obd_enable_sanitizers target)
    if(ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_library(obdcore
    src/core/EnhancedPidProfile.cpp
    src/core/ObdTypes.cpp
    src/core/ObdParser.cpp
    src/core/PidRegistry.cpp
    src/core/PosixElm327Transport.cpp
    src/core/ScanSession.cpp
    src/core/SimulatorTransport.cpp
)

target_include_directories(obdcore PUBLIC src)
obd_enable_warnings(obdcore)
obd_enable_sanitizers(obdcore)

add_executable(370obd2_cli src/main.cpp)
target_link_libraries(370obd2_cli PRIVATE obdcore)
obd_enable_warnings(370obd2_cli)
obd_enable_sanitizers(370obd2_cli)

if(BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets Network SerialPort Pdf PdfWidgets)
    find_package(Qt6Keychain QUIET)
    if(Qt6_FOUND AND Qt6Keychain_FOUND)
        qt_standard_project_setup()
        qt_add_executable(370obd2_gui
            src/gui/MainWindow.cpp
            src/gui/MainWindow.hpp
            src/gui/LiveDataChart.cpp
            src/gui/LiveDataChart.hpp
            src/gui/ScanWorker.cpp
            src/gui/ScanWorker.hpp
            src/gui/main_gui.cpp
        )
        target_link_libraries(370obd2_gui PRIVATE
            obdcore Qt6::Widgets Qt6::Network Qt6::SerialPort Qt6::Pdf Qt6::PdfWidgets
            Qt6Keychain::Qt6Keychain
        )
        target_compile_definitions(370obd2_gui PRIVATE
            OBD_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        obd_enable_warnings(370obd2_gui)
        obd_enable_sanitizers(370obd2_gui)
    else()
        message(WARNING "Qt6 GUI components or Qt6Keychain were not found; GUI target will not be built.")
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(obdcore_tests tests/obdcore_tests.cpp)
    target_link_libraries(obdcore_tests PRIVATE obdcore)
    obd_enable_warnings(obdcore_tests)
    obd_enable_sanitizers(obdcore_tests)
    add_test(NAME obdcore_tests COMMAND obdcore_tests)
    set_tests_properties(obdcore_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    if(ENABLE_SANITIZERS AND NOT MSVC)
        # LeakSanitizer cannot run in some sandboxed/ptraced environments;
        # AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
        set_tests_properties(obdcore_tests PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
        )
    endif()
endif()
