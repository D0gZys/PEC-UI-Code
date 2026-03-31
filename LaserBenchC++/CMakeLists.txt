cmake_minimum_required(VERSION 3.21)

project(LaserBenchCpp VERSION 0.1.0 LANGUAGES C CXX)

if (NOT WIN32)
    message(FATAL_ERROR "LaserBenchC++ currently requires Windows for the Newport motor integration.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)

include(InstallRequiredSystemLibraries)

set(NEWPORT_DOTNET_DLL "${CMAKE_CURRENT_SOURCE_DIR}/../MotorController/lib/Newport.CONEXCC.CommandInterface.dll")
set(BIOLOGIC_SDK_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../Potentiostat/Examples/C-C++/include")
set(BIOLOGIC_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Potentiostat/lib")
set(THORLABS_CAMERA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Camera/SDK/Native Toolkit")
set(THORLABS_CAMERA_DLL_DIR "${THORLABS_CAMERA_SDK_DIR}/dlls/Native_64_lib")
set(THORLABS_CAMERA_LOAD_C "${THORLABS_CAMERA_SDK_DIR}/load_dll_helpers/tl_camera_sdk_load.c")
set(THORLABS_CAMERA_LOAD_H "${THORLABS_CAMERA_SDK_DIR}/load_dll_helpers/tl_camera_sdk_load.h")
set(THORLABS_MONO_TO_COLOR_LOAD_C "${THORLABS_CAMERA_SDK_DIR}/load_dll_helpers/tl_mono_to_color_processing_load.c")
set(THORLABS_MONO_TO_COLOR_LOAD_H "${THORLABS_CAMERA_SDK_DIR}/load_dll_helpers/tl_mono_to_color_processing_load.h")

if (NOT EXISTS "${NEWPORT_DOTNET_DLL}")
    message(FATAL_ERROR "Required Newport .NET assembly not found: ${NEWPORT_DOTNET_DLL}")
endif()
if (NOT EXISTS "${THORLABS_CAMERA_SDK_DIR}/include/tl_camera_sdk.h")
    message(FATAL_ERROR "Required Thorlabs Native Toolkit headers not found: ${THORLABS_CAMERA_SDK_DIR}")
endif()
if (NOT EXISTS "${THORLABS_CAMERA_DLL_DIR}/thorlabs_tsi_camera_sdk.dll")
    message(FATAL_ERROR "Required Thorlabs camera DLLs not found: ${THORLABS_CAMERA_DLL_DIR}")
endif()

file(TO_NATIVE_PATH "${NEWPORT_DOTNET_DLL}" NEWPORT_DOTNET_DLL_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/hardware/NewportConexHelper.cs" HELPER_CS_SOURCE_NATIVE)

if (EXISTS "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
    set(CSHARP_COMPILER "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
    set(CSHARP_FRAMEWORK_DIR "C:/Windows/Microsoft.NET/Framework64/v4.0.30319")
elseif (EXISTS "C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe")
    set(CSHARP_COMPILER "C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe")
    set(CSHARP_FRAMEWORK_DIR "C:/Windows/Microsoft.NET/Framework/v4.0.30319")
else()
    message(FATAL_ERROR "csc.exe not found. A .NET Framework compiler is required to build the Newport helper.")
endif()

file(TO_NATIVE_PATH "${CSHARP_FRAMEWORK_DIR}/System.Web.Extensions.dll" CSHARP_WEB_EXTENSIONS_DLL)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(LaserBench
    src/main.cpp
    src/core/AppState.cpp
    src/core/AppState.hpp
    src/hardware/DeviceContracts.hpp
    src/hardware/BioLogicController.cpp
    src/hardware/BioLogicController.hpp
    src/hardware/MockHardware.cpp
    src/hardware/MockHardware.hpp
    src/hardware/NewportConexController.cpp
    src/hardware/NewportConexController.hpp
    src/hardware/ThorlabsCameraController.cpp
    src/hardware/ThorlabsCameraController.hpp
    src/ui/CameraPreviewWidget.cpp
    src/ui/CameraPreviewWidget.hpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/PotentiostatGraphWidget.cpp
    src/ui/PotentiostatGraphWidget.hpp
    src/ui/PotentiostatHeatmapWidget.cpp
    src/ui/PotentiostatHeatmapWidget.hpp
    src/ui/Potentiostat3DWidget.cpp
    src/ui/Potentiostat3DWidget.hpp
    "${THORLABS_CAMERA_LOAD_C}"
    "${THORLABS_CAMERA_LOAD_H}"
    "${THORLABS_MONO_TO_COLOR_LOAD_C}"
    "${THORLABS_MONO_TO_COLOR_LOAD_H}"
)

target_include_directories(LaserBench PRIVATE
    src
    "${THORLABS_CAMERA_SDK_DIR}/include"
    "${THORLABS_CAMERA_SDK_DIR}/load_dll_helpers"
    "${BIOLOGIC_LIB_DIR}/../Examples/C-C++/include"
)

target_link_libraries(LaserBench PRIVATE Qt6::Widgets)

set_target_properties(LaserBench PROPERTIES
    WIN32_EXECUTABLE TRUE
)

add_custom_command(TARGET LaserBench POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSRC_FILE=${CMAKE_CURRENT_SOURCE_DIR}/calibration.json"
        "-DDST_FILE=$<TARGET_FILE_DIR:LaserBench>/calibration.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyIfNotExists.cmake"
    COMMENT "Deploying default calibration.json (if not already present)"
    VERBATIM
)

add_custom_command(TARGET LaserBench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NEWPORT_DOTNET_DLL}"
        "$<TARGET_FILE_DIR:LaserBench>/Newport.CONEXCC.CommandInterface.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${BIOLOGIC_LIB_DIR}"
        "$<TARGET_FILE_DIR:LaserBench>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${THORLABS_CAMERA_DLL_DIR}"
        "$<TARGET_FILE_DIR:LaserBench>"
    COMMAND ${CMAKE_COMMAND} -E env
        --unset=LIB
        "${CSHARP_COMPILER}"
        /nologo
        /target:exe
        /platform:x64
        "/out:$<TARGET_FILE_DIR:LaserBench>/NewportConexHelper.exe"
        "/reference:${NEWPORT_DOTNET_DLL_NATIVE}"
        "/reference:${CSHARP_WEB_EXTENSIONS_DLL}"
        "${HELPER_CS_SOURCE_NATIVE}"
    VERBATIM
)

if (CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    set(LASERBENCH_RUNTIME_COPY_COMMANDS)
    foreach(RUNTIME_DLL IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        list(APPEND LASERBENCH_RUNTIME_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:LaserBench>"
        )
    endforeach()

    add_custom_command(TARGET LaserBench POST_BUILD
        ${LASERBENCH_RUNTIME_COPY_COMMANDS}
        COMMENT "Deploying MSVC runtime dependencies"
        VERBATIM
    )
endif()

if (TARGET Qt6::qmake)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    get_filename_component(QT_PREFIX_DIR "${QT_BIN_DIR}" DIRECTORY)
    set(QT_PLUGINS_DIR "${QT_PREFIX_DIR}/plugins")
    set(WINDEPLOYQT_EXECUTABLE "${QT_BIN_DIR}/windeployqt.exe")

    add_custom_command(TARGET LaserBench POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DTARGET_FILE=$<TARGET_FILE:LaserBench>"
            "-DTARGET_DIR=$<TARGET_FILE_DIR:LaserBench>"
            "-DQT_BIN_DIR=${QT_BIN_DIR}"
            "-DQT_PLUGINS_DIR=${QT_PLUGINS_DIR}"
            "-DWINDEPLOYQT_EXECUTABLE=${WINDEPLOYQT_EXECUTABLE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
        COMMENT "Deploying Qt runtime dependencies"
        VERBATIM
    )
else()
    message(WARNING "Qt6::qmake target not found; Qt runtime deployment will be skipped.")
endif()

install(TARGETS LaserBench
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
