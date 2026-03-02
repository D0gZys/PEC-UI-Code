#####################################################################
# This document is a part of the BioLogic OEM Package and is
# protected by the terms of the OEM Package licence as well as
# other intellectual property rights owned by BioLogic SAS. 
# This document may only be used for non-commercial purposes 
# such as for the integration of BioLogic equipment to larger 
# technical solutions manufactured  and/or delivered to end-users.
#####################################################################
cmake_minimum_required(VERSION 3.20)

project(OEM_package_examples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/output")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_BUILD_TYPE Release)

# Include OEM Package
include(cmake/oem_package.cmake)

# Add examples
add_subdirectory(device_and_channel_info)
add_subdirectory(technique_ocv)
add_subdirectory(technique_cp)
add_subdirectory(technique_ca)
