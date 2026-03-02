#####################################################################
# This document is a part of the BioLogic OEM Package and is
# protected by the terms of the OEM Package licence as well as
# other intellectual property rights owned by BioLogic SAS. 
# This document may only be used for non-commercial purposes 
# such as for the integration of BioLogic equipment to larger 
# technical solutions manufactured  and/or delivered to end-users.
#####################################################################
add_executable(technique_cp main.cpp)
target_link_libraries(technique_cp PRIVATE ECLib64)
set_target_properties(technique_cp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_target_properties(technique_cp PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_target_properties(technique_cp PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(TARGETS technique_cp DESTINATION Examples/C-C++)
