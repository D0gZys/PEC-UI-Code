@REM ####################################################################
@REM This document is a part of the BioLogic OEM Package and is
@REM protected by the terms of the OEM Package licence as well as
@REM other intellectual property rights owned by BioLogic SAS. 
@REM This document may only be used for non-commercial purposes 
@REM such as for the integration of BioLogic equipment to larger 
@REM technical solutions manufactured  and/or delivered to end-users.
@REM ###################################################################
@echo off
pushd %~dp0
if not exist build mkdir build
REM del /s /q build\* > /dev/null
pushd build
cmake ..
cmake --build . --config Debug
popd
popd