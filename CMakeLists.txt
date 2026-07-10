cmake_minimum_required(VERSION 3.20)

project(370obd2 VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6Keychain REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GUI "Build the Qt desktop GUI" ON)
option(BUILD_TESTS "Build unit tests" ON)

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

add_executable(370obd2_cli src/main.cpp)
target_link_libraries(370obd2_cli PRIVATE obdcore)

if(BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets Network SerialPort Pdf PdfWidgets)
    if(Qt6_FOUND)
        qt_standard_project_setup()
        qt_add_executable(370obd2_gui
            src/gui/MainWindow.cpp
            src/gui/MainWindow.hpp
            src/gui/main_gui.cpp
        )
        target_link_libraries(370obd2_gui PRIVATE
            obdcore Qt6::Widgets Qt6::Network Qt6::SerialPort Qt6::Pdf Qt6::PdfWidgets
            Qt6Keychain::Qt6Keychain
        )
        target_compile_definitions(370obd2_gui PRIVATE
            OBD_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
    else()
        message(WARNING "Qt6 Widgets/SerialPort were not found; GUI target will not be built.")
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(obdcore_tests tests/obdcore_tests.cpp)
    target_link_libraries(obdcore_tests PRIVATE obdcore)
    add_test(NAME obdcore_tests COMMAND obdcore_tests)
    set_tests_properties(obdcore_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
