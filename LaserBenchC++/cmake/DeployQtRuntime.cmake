cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED TARGET_DIR OR TARGET_DIR STREQUAL "")
    message(FATAL_ERROR "TARGET_DIR must be provided to DeployQtRuntime.cmake")
endif()
if (NOT DEFINED TARGET_FILE OR TARGET_FILE STREQUAL "")
    message(FATAL_ERROR "TARGET_FILE must be provided to DeployQtRuntime.cmake")
endif()
if (NOT DEFINED QT_BIN_DIR OR QT_BIN_DIR STREQUAL "")
    message(FATAL_ERROR "QT_BIN_DIR must be provided to DeployQtRuntime.cmake")
endif()

if (NOT DEFINED QT_PLUGINS_DIR OR QT_PLUGINS_DIR STREQUAL "")
    get_filename_component(_qt_prefix_dir "${QT_BIN_DIR}" DIRECTORY)
    set(QT_PLUGINS_DIR "${_qt_prefix_dir}/plugins")
endif()

file(MAKE_DIRECTORY "${TARGET_DIR}")

function(copy_runtime_file source_path destination_dir)
    if (NOT EXISTS "${source_path}")
        return()
    endif()

    file(MAKE_DIRECTORY "${destination_dir}")
    get_filename_component(_file_name "${source_path}" NAME)
    file(COPY_FILE "${source_path}" "${destination_dir}/${_file_name}" ONLY_IF_DIFFERENT)
endfunction()

set(_qt_deployed FALSE)
set(_qtpaths_executable "${QT_BIN_DIR}/qtpaths.exe")

if (DEFINED WINDEPLOYQT_EXECUTABLE
    AND NOT WINDEPLOYQT_EXECUTABLE STREQUAL ""
    AND EXISTS "${WINDEPLOYQT_EXECUTABLE}")
    execute_process(
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --qtpaths "${_qtpaths_executable}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            "${TARGET_FILE}"
        WORKING_DIRECTORY "${QT_BIN_DIR}"
        RESULT_VARIABLE _windeployqt_result
        OUTPUT_VARIABLE _windeployqt_stdout
        ERROR_VARIABLE _windeployqt_stderr
    )

    if (_windeployqt_result EQUAL 0 AND EXISTS "${TARGET_DIR}/platforms/qwindows.dll")
        set(_qt_deployed TRUE)
    else()
        message(WARNING
            "windeployqt failed or produced an incomplete deployment. "
            "Falling back to manual Qt runtime copy.\n"
            "stdout:\n${_windeployqt_stdout}\n"
            "stderr:\n${_windeployqt_stderr}"
        )
    endif()
endif()

if (NOT _qt_deployed)
    foreach(_qt_dll IN ITEMS Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll)
        copy_runtime_file("${QT_BIN_DIR}/${_qt_dll}" "${TARGET_DIR}")
    endforeach()

    copy_runtime_file("${QT_PLUGINS_DIR}/platforms/qwindows.dll" "${TARGET_DIR}/platforms")

    foreach(_style_plugin IN ITEMS qmodernwindowsstyle.dll qwindowsvistastyle.dll)
        copy_runtime_file("${QT_PLUGINS_DIR}/styles/${_style_plugin}" "${TARGET_DIR}/styles")
    endforeach()

    foreach(_image_plugin IN ITEMS qgif.dll qico.dll qjpeg.dll qsvg.dll qtiff.dll)
        copy_runtime_file("${QT_PLUGINS_DIR}/imageformats/${_image_plugin}" "${TARGET_DIR}/imageformats")
    endforeach()

    copy_runtime_file("${QT_PLUGINS_DIR}/iconengines/qsvgicon.dll" "${TARGET_DIR}/iconengines")
endif()
