#####################################################################
# This document is a part of the BioLogic OEM Package and is
# protected by the terms of the OEM Package licence as well as
# other intellectual property rights owned by BioLogic SAS. 
# This document may only be used for non-commercial purposes 
# such as for the integration of BioLogic equipment to larger 
# technical solutions manufactured  and/or delivered to end-users.
#####################################################################
#- OEM Package -----------------------------------------------------------------------
if ( NOT $ENV{ECLIB_DIR} STREQUAL "" )
  set(OEM_DIR $ENV{ECLIB_DIR})
else()
  message ( STATUS "OEM Package> ECLIB_DIR                         = $ENV{ECLIB_DIR}" )
  set (OEM_DIR "C:/EC-Lab Development Package/lib")
endif()
message ( STATUS "OEM Package> OEM_DIR                           = ${OEM_DIR}" )
set (OEM_INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)
message ( STATUS "OEM Package> OEM_INTERFACE_INCLUDE_DIRECTORIES = ${OEM_INTERFACE_INCLUDE_DIRECTORIES}" )

#- ECLib -----------------------------------------------------------------------------
set (ECLIB_IMPORTED_IMPLIB ${OEM_DIR}/ECLib64.lib)
set (ECLIB_IMPORTED_LOCATION  ${OEM_DIR}/ECLib64.dll)
message ( STATUS "OEM Package> ECLIB_IMPORTED_IMPLIB             = ${ECLIB_IMPORTED_IMPLIB}" )
message ( STATUS "OEM Package> ECLIB_IMPORTED_LOCATION           = ${ECLIB_IMPORTED_LOCATION}" )

add_library( ECLib64 SHARED IMPORTED GLOBAL)
set_target_properties(
  ECLib64
  PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${OEM_INTERFACE_INCLUDE_DIRECTORIES}
  IMPORTED_IMPLIB ${ECLIB_IMPORTED_IMPLIB}
  IMPORTED_LOCATION  ${ECLIB_IMPORTED_LOCATION}
)

#- BLFind ----------------------------------------------------------------------------
set (BLFIND_IMPORTED_IMPLIB ${OEM_DIR}/BLFind64.lib)
set (BLFIND_IMPORTED_LOCATION  ${OEM_DIR}/BLFind64.dll)
message ( STATUS "OEM Package> BLFIND_IMPORTED_IMPLIB            = ${BLFIND_IMPORTED_IMPLIB}" )
message ( STATUS "OEM Package> BLFIND_IMPORTED_LOCATION          = ${BLFIND_IMPORTED_LOCATION}" )

add_library( BLFind64 SHARED IMPORTED GLOBAL)
set_target_properties(
  BLFind64
  PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${OEM_INTERFACE_INCLUDE_DIRECTORIES}
  IMPORTED_IMPLIB ${BLFIND_IMPORTED_IMPLIB}
  IMPORTED_LOCATION  ${BLFIND_IMPORTED_LOCATION}
)

#- Install libs -----------------------------------------------------------------------
message ( STATUS "OEM Package> install ${ECLIB_IMPORTED_LOCATION}  => ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/")
message ( STATUS "OEM Package> install ${BLFIND_IMPORTED_LOCATION} => ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/")
message ( STATUS "COPY ${OEM_DIR}/ DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ )")
file( COPY ${OEM_DIR}/ DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ )
