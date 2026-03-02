// -----------------------------------------------------------------
// This document is a part of the BioLogic OEM Package and is
// protected by the terms of the OEM Package licence as well as
// other intellectual property rights owned by BioLogic SAS.
// This document may only be used for non-commercial purposes
// such as for the integration of BioLogic equipment to larger
// technical solutions manufactured  and/or delivered to end-users.
// -----------------------------------------------------------------
#pragma once

#include "BLStructs.h"
#include "stdint.h"

#define BIOLOGIC_API( TYPE ) extern "C" TYPE _stdcall

/*
 * Bio-Logic Header file for ECLib C/C++ interface
 * (c) Bio-Logic 2013
 */

/**
 * \mainpage
 * This documentation describes how to use the Bio-Logic ECLib development package in C and C++,
 * and provides a set of functions and structures to ease this usage.
 *
 * In many cases, this documentation is very close to the one you find in the ECLab Development Package PDF. Feel free
 * to use both if you feel more comfortable with the PDF. The main differences here are the C types, instead of Delphi.
 *
 * You can navigate to the [list of modules](modules.html).
 *
 */

/**
 *
 * \defgroup general_functions General Functions
 * @{
 */

/**
 * This function copies the version of the library into the buffer.
 *
 * @param pVersion pointer to the buffer that will receive the text (C-string format)
 * @param psize pointer to a unsigned int who defines the maximum number of
 *              characters of the buffer. It also returns the number of characters
 *              of the copied string.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetLibVersion( char* pVersion, unsigned int* psize );

/**
 * This function returns volume serial number.
 *
 * \note the serial number of a (logical) drive is generated every time a
 * drive is formatted. When Windows formats a drive, a drive's serial
 * number gets calculated using the current date and time and is stored
 * in the drive's boot sector. The odds of two disks getting the same
 * number are virtually nil on the same machine.
 *
 * @return the unique volume serial number
 */
extern "C" unsigned int _stdcall BL_GetVolumeSerialNumber( void );

/**
 * This function copies into the buffer the corresponding message of the selected error code.
 *
 * @param errorcode error code for which a description is needed
 * @param pmsg pointer to the buffer that will receive the text (C-string format)
 * @param psize pointer to a unsigned int who defines the maximum number of
 *              characters of the buffer. It also returns the number of characters
 *              of the copied string.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetErrorMsg( int errorcode, char* pmsg, unsigned int* psize );

/**@}
 *
 * \defgroup comm_functions Communication Functions
 * @{
 */

/**
 * This function establishes the connection with the selected instrument
 * and copies general informations (device code, RAM size, ...) into the
 * TDEVICEINFOS structure. The identifier (ID) returned must be used
 * in all other routines to communicate with the instrument.
 *
 * @param address  pointer to the buffer who defines the instrument selected (Cstring
 *              format). Ex : 192.109.209.200, USB0, USB1, ...
 * @param timeout communication time-out in second (5 s recommended)
 * @param pID pointer to a int that will receive the device identifier of the instrument
 * @param pInfos pointer to a device informations structure (see \ref TDeviceInfos_t)
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_Connect( const char* address, uint8 timeout, int* pID, TDeviceInfos_t* pInfos );

/**
 * This function closes the connection with the instrument.
 *
 * @param ID device identifier obtained with \ref BL_Connect
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_Disconnect( int ID );

/**
 * This function tests the communication with the selected instrument.
 *
 * @param ID device identifier obtained with \ref BL_Connect
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_TestConnection( int ID );

/**
 * This function tests the communication speed between the library and
 * the selected channel. \note This function is for advanced user only.
 *
 * @param ID device identifier
 * @param channel selected channel (0..15)
 * @param spd_rcvt pointer to a int that will receive the communication speed (in
 *                  ms) between the library and the device
 * @param spd_kernel pointer to a int that will receive the communication speed (in
 *                  ms) between the library and the selected channel
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_TestCommSpeed( int ID, uint8 channel, int* spd_rcvt, int* spd_kernel );

/**
 * This function returns information stored into the USB device selected.
 * \note This function is for advanced user only.
 *
 * @param USBindex index of USB device selected (0-based)
 * @param pcompany pointer to the buffer that will receive the company name (C-string format)
 * @param pcompanysize pointer to a unsigned int who defines the maximum number of
 *                      characters of the buffer. It also returns the number of characters
 *                      of the copied string.
 * @param pdevice pointer to the buffer that will receive the device name (C-string format)
 * @param pdevicesize pointer to a unsigned int who defines the maximum number of
 *                      characters of the buffer. It also returns the number of characters
 *                      of the copied string.
 * @param pSN pointer to the buffer that will receive the device serial number (Cstring format)
 * @param pSNsize pointer to a unsigned int who defines the maximum number of
 *                      characters of the buffer. It also returns the number of characters
 *                      of the copied string.
 * @return true if successful, false otherwise.
 */
extern "C" bool _stdcall BL_GetUSBdeviceinfos( unsigned int USBindex, char* pcompany, unsigned int* pcompanysize, char* pdevice,
                                               unsigned int* pdevicesize, char* pSN, unsigned int* pSNsize );

/** @}
 *
 * \defgroup firmware_functions Firmware functions
 * @{ */

/**
 * This function loads the firmware on selected channels. Be aware that
 * channels are unusable until the firmware is loaded.
 *
 * @param ID device identifier
 * @param pChannels pointer to the array who represents the channels of the device.
 *                  For each element of the array:
 *                      - 0: channel not selected
 *                      - 1: channel selected
 * @param pResults pointer to the array that will receive the result of the function for each channels :
 *                  - =0: the function succeeded
 *                  - <0: see \ref TErrorCodes_e
 * @param Length length of the arrays pointed by pChannels and pResults.
 * @param ShowGauge if TRUE a gauge is shown during the firmware loading.
 * @param ForceReload if TRUE the firmware is loaded each time, if FALSE the
 *                  firmware is loaded only if not already done.
 * @param BinFile Name of bin file (nil for default file).
 * @param XlxFile Name of bin file (nil for default file).
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_LoadFirmware( int ID, uint8* pChannels, int* pResults, uint8 Length, bool ShowGauge, bool ForceReload,
                                         const char* BinFile, const char* XlxFile );

/**@}
 *
 * \defgroup channel_functions Channel information functions
 * @{
 */

/**
 * This function tests if the selected channel is plugged
 * @param ID device identifier
 * @param ch channel
 * @return true if the channel is plugged, false otherwise.
 */
extern "C" bool _stdcall BL_IsChannelPlugged( int ID, uint8 ch );

/**
 *
 * @param ID device identifier
 * @param pChPlugged pointer to the array who represents the channels of the device.
 *                  For each element of the array :
 *                          - 0 = channel not plugged
 *                          - 1 = channel plugged
 * @param Size size of the array pointed by pChPlugged
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetChannelsPlugged( int ID, uint8* pChPlugged, uint8 Size );

/**
 * This function copies information of selected channel into the \ref TChannelInfos_t structure.
 * @param ID device identifier
 * @param ch channel selected (0 .. 15)
 * @param pInfos pointer on a channel informations structure.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetChannelInfos( int ID, uint8 ch, TChannelInfos_t* pInfos );

/**
 * This function copies into the buffer the messages generated by the
 * firmware of selected channel. Be aware that messages are retrieved
 * one-by-one, this implies this function must be called several times in
 * order to get all messages available in the message queue.
 *
 * @param ID device identifier
 * @param ch channel selected (0 .. 15)
 * @param msg pointer to the buffer that will receive the text (C-string format)
 * @param size pointer to a unsigned int who defines the maximum number of
 *              characters of the buffer. It also returns the number of characters
 *              of the copied string.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetMessage( int ID, uint8 ch, char* msg, unsigned int* size );

/**
 * This function return in pHardConf the hardware configuration of the
 * channel ch. The hardware configuration is the electrode connection
 * and the instrument ground.
 * This function must be used only with SP-300 series.
 *
 * @param ID device identifier
 * @param ch channel selected (0 .. 15)
 * @param pHardConf pointer to a \ref THardwareConf_t object
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetHardConf( int ID, uint8 ch, THardwareConf_t* pHardConf );

/**
 * This function set the hardware configuration of a channel with HardConf object.
 * This function must be used only with SP-300 series.
 *
 * @param ID device identifier
 * @param ch channel selected (0 .. 15)
 * @param HardConf \ref THardwareConf_t object. The attribute "Conn" is the electrode
 *              connection mode and the attribute "Ground" is the instrument
 *              ground. See \ref TElectrodeMode_e and \ref TElectrodeConn_e to have
 *              the value of these attributes.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_SetHardConf( int ID, uint8 ch, THardwareConf_t HardConf );

/**
 * @}
 *
 * \defgroup technique_functions Technique Functions
 * @{
 */

/**
 * This function loads a technique and its parameters on the selected channel.
 * \note To run linked techniques, this function must be called for each selected technique.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param pFName pointer to the buffer who contains the name of the *.ecc file who
 *              defines the technique (C-string format)
 * @param Params structure of parameters of selected technique.See section 7. \em Techniques
 *              in the PDF for a complete description of parameters available for each technique.
 * @param FirstTechnique TRUE if the technique loaded is the first one
 * @param LastTechnique TRUE if the technique loaded is the last one
 * @param DisplayParams display the parameters that are sent (for debugging purposes)
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_LoadTechnique( int ID, uint8 channel, const char* pFName, TEccParams_t Params, bool FirstTechnique,
                                          bool LastTechnique, bool DisplayParams );

/**
 * This function must be used to populate \ref TEccParam_t structure with a boolean.
 *
 * @param lbl parameter label (C-string format)
 * @param value parameter value (boolean)
 * @param index parameter index (useful only for multi-step parameters)
 * @param pParam pointer on a elementary technique parameter structure (see \ref TEccParam_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_DefineBoolParameter( const char* lbl, bool value, int index, TEccParam_t* pParam );

/**
 * This function must be used to populate \ref TEccParam_t structure with a float.
 *
 * @param lbl parameter label (C-string format)
 * @param value parameter value (float)
 * @param index parameter index (useful only for multi-step parameters)
 * @param pParam pointer on a elementary technique parameter structure (see \ref TEccParam_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_DefineSglParameter( const char* lbl, float value, int index, TEccParam_t* pParam );

/**
 * This function must be used to populate \ref TEccParam_t structure with an integer.
 *
 * @param lbl parameter label (C-string format)
 * @param value parameter value (int)
 * @param index parameter index (useful only for multi-step parameters)
 * @param pParam pointer on a elementary technique parameter structure (see \ref TEccParam_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_DefineIntParameter( const char* lbl, int value, int index, TEccParam_t* pParam );

/**
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param TechIndx index of the technique if several techniques have been started.
 *                  The first have index 0, the second have index 1, ...
 * @param Params structure of parameters of selected technique.See section 7. \em Techniques
 *                  in the PDF for a complete description of parameters available for each technique.
 * @param EccFileName pointer to the buffer who contains the name of the *.ecc file who
 *                  defines the technique (C-string format)
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_UpdateParameters( int ID, uint8 channel, int TechIndx, TEccParams_t Params, const char* EccFileName );

/**
 * @}
 *
 * \defgroup start_stop_functions Start/Stop functions
 * @{
 */

/**
 * This function starts technique(s) loaded on selected channel.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_StartChannel( int ID, uint8 channel );

/**
 * This function starts technique(s) loaded on selected channels.
 *
 * @param ID device identifier
 * @param pChannels pointer to the array who represents the channels of the device.
 *                  For each element of the array:
 *                      - 0: channel not selected
 *                      - 1: channel selected
 * @param pResults pointer to the array that will receive the result of the function for
 *                  each channels. See \ref TErrorCodes_e.
 * @param length length of the arrays pointed by pChannels and pResults.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_StartChannels( int ID, uint8* pChannels, int* pResults, uint8 length );

/**
 * This function stops technique(s) loaded on selected channel.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_StopChannel( int ID, uint8 channel );

/**
 * This function stops technique(s) loaded on selected channels.
 *
 * @param ID device identifier
 * @param pChannels pointer to the array who represents the channels of the device.
 *                  For each element of the array:
 *                      - 0: channel not selected
 *                      - 1: channel selected
 * @param pResults pointer to the array that will receive the result of the function for
 *                  each channels. See \ref TErrorCodes_e.
 * @param length length of the arrays pointed by pChannels and pResults.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_StopChannels( int ID, uint8* pChannels, int* pResults, uint8 length );

/**
 * @}
 *
 * \defgroup data_functions Data functions
 * @{
 */

/**
 * This function copies current values (Ewe, Ece, I, t, ...) from selected
 * channel into the structure \ref TCurrentValues_t.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param pValues pointer to a current values structure (see \ref TCurrentValues_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetCurrentValues( int ID, uint8 channel, TCurrentValues_t* pValues );

/**
 * This function copies data from selected channel into the buffer and
 * copies informations into the  \ref TDataInfos_t structure.
 *
 * The field TECHNIQUEID of the structure \ref TDataInfos_t contains the ID
 * of the technique used to record data. Thanks to this technique ID one
 * can identify the format of the data saved into the buffer (see section
 * 7. Techniques in the PDF for a complete description of data format for each
 * technique).
 *
 * Be aware that techniques can also be composed of several process
 * (PEIS and GEIS for instance). In this case one can identify the
 * process used to record data with the field PROCESSINDEX of the
 * structure \ref TDataInfos_t.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param pBuf pointer to the buffer that will receive data.
 * @param pInfos pointer to a data informations structure (see \ref TDataInfos_t )
 * @param pValues  pointer to a current values structure (see \ref TCurrentValues_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetData( int ID, uint8 channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues );

/**
 * This function copies data from selected channel into the buffer and
 * copies informations into the \ref TDataInfos_t structure.
 * \note Function for advanced user only.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param pBuf pointer to the buffer that will receive data.
 * @param pInfos pointer to a data informations structure (see \ref TDataInfos_t )
 * @param pValues  pointer to a current values structure (see \ref TCurrentValues_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetFCTData( int ID, uint8 channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues );

/**
 * This function converts a numerical value coming from the data buffer
 * of BL_GETDATA function into float.
 *
 * @param num numerical value
 * @param psgl pointer to the float that will receive the result of conversion
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_ConvertNumericIntoSingle( unsigned int num, float* psgl );

/**
 * @}
 *
 * \defgroup miscellaneous Miscellaneous functions
 * @{
 */

/**
 * This function saves experiment informations on selected channel.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param TExpInfos Experiment informations (see \ref TExperimentInfos_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_SetExperimentInfos( int ID, uint8 channel, TExperimentInfos_t TExpInfos );

/**
 * This function copies experiment informations from selected channel
 * into the  \ref TExperimentInfos_t structure.
 *
 * @param ID device identifier
 * @param channel selected channel (0 .. 15)
 * @param TExpInfos pointer to an experiment informations structure (see \ref TExperimentInfos_t )
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_GetExperimentInfos( int ID, uint8 channel, TExperimentInfos_t* TExpInfos );

/**
 * This function sends a message to the selected channel.
 * These messages are sent in the form of strings and can be useful to debug your experiment.
 * \note Function for advanced user only.
 *
 * @param ID device identifier
 * @param ch selected channel (0 .. 15)
 * @param pBuf pointer to the data buffer
 * @param pLen pointer to a unsigned int who defines the length of data to transfer. It
 *              also returns the length of data copied into the buffer.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_SendMsg( int ID, uint8 ch, void* pBuf, unsigned int* pLen );

/**
 * This function updates the communication firmware of the instrument.
 * \note Function for advanced users only.
 *
 * @param ID device identifier
 * @param pfname file name (*.flash extension) of the new communication firmware (C-string format)
 * @param ShowGauge show a gauge during transfer
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_LoadFlash( int ID, const char* pfname, bool ShowGauge );

/**
 *
 */
extern "C" int _stdcall BL_GetChannelBoardType( int32_t ID, uint8 Channel, uint32_t* pChannelType );

/**
 *
 */
extern "C" int _stdcall BL_ConvertChannelNumericIntoSingle( uint32_t num, float* pRetFloat, uint32_t ChannelType );

/**
 *
 */
extern "C" int _stdcall BL_ConvertTimeChannelNumericIntoSeconds( uint32_t* pnum, double* pRetTime, float Timebase, uint32_t ChannelType );

/** @} */

/**
 *
 * \defgroup blfind_functions BLFind Functions
 * @{
 */

/**
 * This function finds Ethernet and USB electrochemistry instruments and copies into the buffer a serialization of descriptions of detected
 * instruments..
 *
 * @param pLstDev pointer to the buffer that will receive the serialization of instruments description
 * @param psize pointer to a uint32 specifying the maximum number of characters of the buffer. It also returns the number of characters of
 * the copied serialization.
 * @param pNbrDevice pointer to a uint32 receiving the number of detected Ethernet and USB instruments.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_FindEChemDev( char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice );

/**
 * This function finds Ethernet electrochemistry instruments and copies into the buffer a serialization of descriptions of detected
 * instruments.
 *
 * @param pLstDev pointer to the buffer that will receive the serialization of instruments description
 * @param psize pointer to a uint32 specifying the maximum number of characters of the buffer. It also returns the number of characters of
 * the copied serialization.
 * @param pNbrDevice pointer to a uint32 receiving the number of detected Ethernet and USB instruments.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_FindEChemEthDev( char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice );

/**
 * This function his function finds USB electrochemistry instruments and copies into the buffer a serialization of descriptions of the
 * detected instruments.
 *
 * @param pLstDev pointer to the buffer that will receive the serialization of instruments description
 * @param psize pointer to a uint32 specifying the maximum number of characters of the buffer. It also returns the number of characters of
 * the copied serialization.
 * @param pNbrDevice pointer to a uint32 receiving the number of detected Ethernet and USB instruments.
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_FindEChemUsbDev( char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice );

/**
 * This function sets new TCP/IP parameters of selected instrument. IP address, netmask and gateway may be modified.
 *
 * @param pIp pointer to the buffer specifying the IP address of the instrument to configure.
 * @param pCfg pointer to the buffer specifying the new TCP/IP parameters of the instrument
 * @return \ref ERR_NOERROR if successful, another value if failed, see \ref TErrorCodes_e .
 */
extern "C" int _stdcall BL_SetConfig( char* pIp, char* pCfg );

/** @} */
