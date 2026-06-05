
// -----------------------------------------------------------------
// This document is a part of the BioLogic OEM Package and is
// protected by the terms of the OEM Package licence as well as
// other intellectual property rights owned by BioLogic SAS.
// This document may only be used for non-commercial purposes
// such as for the integration of BioLogic equipment to larger
// technical solutions manufactured  and/or delivered to end-users.
// -----------------------------------------------------------------
#pragma once

#ifndef _BLSTRUCTS_H_
    #define _BLSTRUCTS_H_

/*
 * Bio-Logic Header file for ECLib C/C++ interface
 * (c) Bio-Logic 2013
 */

/**
 * \defgroup structures Structures and constants
 * @{
 */

    #pragma pack( push, 4 ) // needed because default pack size is 8, which produces an offset for some structures

typedef unsigned char uint8; /*!< 8 bits unsigned integer */

/**
 * This structure holds information about the device that \ref BL_Connect connected to.
 */
typedef struct
{
    int DeviceCode;        /*!< Device code see \ref TDeviceType_e */
    int RAMSize;           /*!< RAM size in MB */
    int CPU;               /*!< CPU type */
    int NumberOfChannels;  /*!< Number of channels connected */
    int NumberOfSlots;     /*!< Number of slots available */
    int FirmwareVersion;   /*!< Communication firmware version */
    int FirmwareDate_yyyy; /*!< Communication firmware date YYYY */
    int FirmwareDate_mm;   /*!< Communication firmware date MM */
    int FirmwareDate_dd;   /*!< Communication firmware date DD */
    int HTdisplayOn;       /*!< Allow hyper-terminal prints (true/false) */
    int NbOfConnectedPC;   /*!< Number of connected PCs */
} TDeviceInfos_t;

/**
 * This structure holds information about the channel. You can obtain them using \ref BL_GetChannelInfos
 */
typedef struct
{
    int Channel;           /*!< Channel (0..15) */
    int BoardVersion;      /*!< Board version */
    int BoardSerialNumber; /*!< Board serial number */
    int FirmwareCode;      /*!< Firmware loaded (see \ref TFirmwareCode_e) */
    int FirmwareVersion;   /*!< Firmware version */
    int XilinxVersion;     /*!< Xilinx version */
    int AmpCode;           /*!< Amplifier code (see \ref TAmplifierType_e) */
    int NbAmps;            /*!< Number of amplifiers */
    int Lcboard;           /*!< Low current board present (true/false) */
    int Zboard;            /*!< true if the channel has impedance measurement capability */
    int RESERVED;          /*!< not used */
    int RESERVED2;         /*!< not used */
    int MemSize;           /*!< Memory size (in bytes) */
    int MemFilled;         /*!< Memory filled (in bytes) */
    int State;             /*!< Channel State (see \ref TChannelState_e) */
    int MaxIRange;         /*!< Maximum I range allowed (see \ref TIntensityRange_e) */
    int MinIRange;         /*!< Minimum I range allowed (see \ref TIntensityRange_e) */
    int MaxBandwidth;      /*!< Maximum bandwidth allowed (see \ref TBandwidth_e) */
    int NbOfTechniques;    /*!< Number of techniques loaded */
    int FloatFormat;       /*!< 0 = Ti or 1 = IEEE float format */
    int CPUCIE;            /*!< CPU board type */
} TChannelInfos_t;

/**
 * This structure contains information about the channel current values measurement.
 */
typedef struct
{
    int   State;       /*!< Channel state: see \ref TChannelState_e. */
    int   MemFilled;   /*!< Memory filled (in Bytes) */
    float TimeBase;    /*!< Time base (s) */
    float Ewe;         /*!< Working electrode potential (V) */
    float EweRangeMin; /*!< Ewe min range (V) */
    float EweRangeMax; /*!< Ewe max range (V) */
    float Ece;         /*!< Counter electrode potential (V) */
    float EceRangeMin; /*!< Ece min range (V) */
    float EceRangeMax; /*!< Ece max range (V) */
    int   Eoverflow;   /*!< Potential overflow */
    float I;           /*!< Current value (A) */
    int   IRange;      /*!< Current range (see \ref TIntensityRange_e) */
    int   Ioverflow;   /*!< Current overflow */
    float ElapsedTime; /*!< Elapsed time (s) */
    float Freq;        /*!< Frequency (Hz) */
    float Rcomp;       /*!< R compensation (Ohm) */
    int   Saturation;  /*!< E or/and I saturation */
    int   OptErr;      /*!< Hardware option error code (see \ref TErrorCodes_e, SP-300 series only) */
    int   OptPos;      /*!< Index of the option generating the OptErr (SP-300 series only) */
} TCurrentValues_t;

/**
 * This structure holds metadata about the data you just received with \ref BL_GetData
 */
typedef struct
{
    int IRQskipped;     /*!< Number of IRQ skipped */
    int NbRows;         /*!< Number of rows into the data buffer, i.e.number of points saved in the data buffer */
    int NbCols;         /*!< Number of columns into the data buffer, i.e. number of variables defining a point in the data buffer */
    int TechniqueIndex; /*!< Index (0-based) of the technique who has generated the data. This field is only useful for linked techniques */
    int TechniqueID;    /*!< Identifier of the technique who has generated the data. Must be used to identify the data format into the data
                           buffer (see \ref TTechniqueIdentifier_e ) */
    int ProcessIndex;   /*!< Index (0-based) of the process of the technique who has generated the data. Must be used to identify the data
                           format into the data buffer */
    int    loop;        /*!< Loop number */
    double StartTime;   /*!< Start time (s) */
    int    MuxPad;      /*!< Active MP-MEA option pad number (SP-300 series only) */
} TDataInfos_t;

/**
 * This structure is used to retrieve data from the device by the function \ref BL_GetData
 */
typedef struct
{
    unsigned int data[1000]; /*!< Buffer of size 1000 * 4 bytes */
} TDataBuffer_t;

/**
 * The structure \ref TEccParam_t defines an elementary technique parameter and is used by
 * the function \ref BL_LoadTechnique
 */
typedef struct
{
    char ParamStr[64]; /*!< string who defines the parameter label (see section 7. Techniques in PDF for a complete description of
                          parameters available for each technique) */
    int ParamType;     /*!< Parameter type (see \ref TParamType_e) */
    int ParamVal;      /*!< Parameter value. \warning Numerical value */
    int ParamIndex;    /*!< Parameter index (0-based), useful for multi-step parameters. Otherwise should be 0. */
} TEccParam_t;

/**
 * The structure \ref TEccParams_t defines an array of elementary technique parameters and is used by the function \ref BL_LoadTechnique
 */
typedef struct
{
    int          len;     /*!< Length of the array pointed by pParams */
    TEccParam_t* pParams; /*!< Pointer on the array of technique parameters (array of structure \ref TEccParam_t) */
} TEccParams_t;

/** The structure \ref THardwareConf_t describes the channel electrode configuration. See \ref BL_GetHardConf and \ref BL_SetHardConf */
typedef struct
{
    int Conn;   /*!< Electrode connection (see \ref TElectrodeConn_e) */
    int Ground; /*!< Instrument ground (see \ref TElectrodeMode_e) */
} THardwareConf_t;

/** Technique informations */
typedef struct
{
    int          id;           /*!< technique id */
    int          indx;         /*!< index of the technique */
    int          nbParams;     /*!< number of parameters */
    int          nbSettings;   /*!< number of hardware settings */
    TEccParam_t* Params;       /*!< pointer to the parameters */
    TEccParam_t* HardSettings; /*!< pointer to the hardware settings */
} TTechniqueInfos;

/** Experiment informations */
typedef struct
{
    int  Group;         /*!< TODO */
    int  PCidentifier;  /*!< TODO */
    int  TimeHMS;       /*!< TODO */
    int  TimeYMD;       /*!< TODO */
    char Filename[256]; /*!< TODO */
} TExperimentInfos_t;

/** This enum is used in \ref BL_Connect and \ref TDeviceInfos_t */
typedef enum
{
    KBIO_DEV_VMP     = 0,  /*!< VMP device */
    KBIO_DEV_VMP2    = 1,  /*!< VMP2 device */
    KBIO_DEV_MPG     = 2,  /*!< MPG device */
    KBIO_DEV_BISTAT  = 3,  /*!< BISTAT device */
    KBIO_DEV_MCS200  = 4,  /*!< MCS-200 device */
    KBIO_DEV_VMP3    = 5,  /*!< VMP3 device */
    KBIO_DEV_VSP     = 6,  /*!< VSP */
    KBIO_DEV_HCP803  = 7,  /*!< HCP-803 */
    KBIO_DEV_EPP400  = 8,  /*!< EPP-400 */
    KBIO_DEV_EPP4000 = 9,  /*!< EPP-4000 */
    KBIO_DEV_BISTAT2 = 10, /*!< BISTAT 2 */
    KBIO_DEV_FCT150S = 11, /*!< FCT-150S */
    KBIO_DEV_VMP300  = 12, /*!< VMP-300 */
    KBIO_DEV_SP50    = 13, /*!< SP-50 */
    KBIO_DEV_SP150   = 14, /*!< SP-150 */
    KBIO_DEV_FCT50S  = 15, /*!< FCT-50S */
    KBIO_DEV_SP300   = 16, /*!< SP300 */
    KBIO_DEV_CLB500  = 17, /*!< CLB-500 */
    KBIO_DEV_HCP1005 = 18, /*!< HCP-1005 */
    KBIO_DEV_CLB2000 = 19, /*!< CLB-2000 */
    KBIO_DEV_VSP300  = 20, /*!< VSP-300 */
    KBIO_DEV_SP200   = 21, /*!< SP-200 */
    KBIO_DEV_MPG2    = 22, /*!< MPG2 */
    KBIO_DEV_SP100   = 23, /*!< SP-100 */
    KBIO_DEV_MOSLED  = 24, /*!< MOSLED */
    KBIO_DEV_KINEXXX = 25, /*!< Kinetic device... \warning unused code */
    KBIO_DEV_NIKITA  = 26, /*!< Nikita */
    KBIO_DEV_SP240   = 27, /*!< SP-240 */
    KBIO_DEV_MPG205  = 28, /*!< MPG-205 (techno VMP3) \warning not controled by kernel2 */
    KBIO_DEV_MPG210  = 29, /*!< MPG-210 (techno VMP3) \warning not controled by kernel2 */
    KBIO_DEV_MPG220  = 30, /*!< MPG-220 (techno VMP3) \warning not controled by kernel2 */
    KBIO_DEV_MPG240  = 31, /*!< MPG-240 (techno VMP3) \warning not controled by kernel2 */
    KBIO_DEV_BP300   = 32, /*!< BP-300 (techno VMP-300)}*/
    KBIO_DEV_VMP3E   = 33, /*!< VMP-3e (16 channels, VMP3 technology)*/
    KBIO_DEV_VSP3E   = 34, /*!< VSP-3e (8 channels, VMP3 technology)*/
    KBIO_DEV_SP50E   = 35, /*!< SP-50e (1 channel, VMP3 technology)*/
    KBIO_DEV_SP150E  = 36, /*!< SP-150e (1 channel, VMP3 technology)*/

    KBIO_DEV_UNKNOWN = 255 /*!< Unknown device */
} TDeviceType_e;

/** Firmware codes */
typedef enum
{
    KIBIO_FIRM_NONE    = 0, /*!< No firmware loaded */
    KIBIO_FIRM_INTERPR = 1, /*!< Firmware for EC-Lab software */
    KIBIO_FIRM_UNKNOWN = 4, /*!< Unknown firmware loaded */
    KIBIO_FIRM_KERNEL  = 5, /*!< Firmware for the library */
    KIBIO_FIRM_INVALID = 8, /*!< Invalid firmware loaded */
    KIBIO_FIRM_ECAL    = 10 /*!< Firmware for calibration software */
} TFirmwareCode_e;

/** Amplifier types */
typedef enum
{
    KIBIO_AMPL_NONE            = 0,  /*!< No amplifier VMP3 series */
    KIBIO_AMPL_2A              = 1,  /*!< Amplifier 2 A VMP3 series */
    KIBIO_AMPL_1A              = 2,  /*!< Amplifier 1 A VMP3 series */
    KIBIO_AMPL_5A              = 3,  /*!< Amplifier 5 A VMP3 series */
    KIBIO_AMPL_10A             = 4,  /*!< Amplifier 10 A VMP3 series */
    KIBIO_AMPL_20A             = 5,  /*!< Amplifier 20 A VMP3 series */
    KIBIO_AMPL_HEUS            = 6,  /*!< reserved VMP3 series */
    KIBIO_AMPL_LC              = 7,  /*!< Low current amplifier VMP3 series */
    KIBIO_AMPL_80A             = 8,  /*!< Amplifier 80 A VMP3 series */
    KIBIO_AMPL_4AI             = 9,  /*!< Amplifier 4 A VMP3 series */
    KIBIO_AMPL_PAC             = 10, /*!< Fuel Cell Tester VMP3 series */
    KIBIO_AMPL_4AI_VSP         = 11, /*!< Amplifier 4 A (VSP instrument) VMP3 series */
    KIBIO_AMPL_LC_VSP          = 12, /*!< Low current amplifier (VSP instrument) VMP3 series */
    KIBIO_AMPL_UNDEF           = 13, /*!< Undefined amplifier VMP3 series */
    KIBIO_AMPL_MUIC            = 14, /*!< reserved VMP3 series */
    KIBIO_AMPL_NONE_GIL        = 15, /*!< No amplifier VMP3 series */
    KIBIO_AMPL_8AI             = 16, /*!< Amplifier 8 A VMP3 series */
    KIBIO_AMPL_LB500           = 17, /*!< Amplifier LB500 VMP3 series */
    KIBIO_AMPL_100A5V          = 18, /*!< Amplifier 100 A VMP3 series */
    KIBIO_AMPL_LB2000          = 19, /*!< Amplifier LB2000 VMP3 series */
    KBIO_AMPL_1A48V            = 20, /*!< Amplifier 1A 48V SP-300 series */
    KBIO_AMPL_4A10V            = 21, /*!< Amplifier 4A 10V SP-300 series */
    KBIO_AMPL_5A_MPG2B         = 22, /*!< MPG-205 5A amplifier  */
    KBIO_AMPL_10A_MPG2B        = 23, /*!< MPG-210 10A amplifier  */
    KBIO_AMPL_20A_MPG2B        = 24, /*!< MPG-220 20A amplifier  */
    KBIO_AMPL_40A_MPG2B        = 25, /*!< MPG-240 40A amplifier  */
    KBIO_AMPL_COIN_CELL_HOLDER = 26, /*!< coin cell holder */
    KBIO_AMPL4_10A5V           = 27, /*!< VMP4 10A/5V amplifier (SP-300 internal amplifier) */
    KBIO_AMPL4_2A30V           = 28, /*!< VMP4 2A/30V */
} TAmplifierType_e;

/** Intensity range */
typedef enum
{
    KBIO_IRANGE_100pA   = 0,  /*!< I range 100 pA SP-300 series */
    KBIO_IRANGE_1nA     = 1,  /*!< I range 1 nA VMP3 / SP-300 series */
    KBIO_IRANGE_10nA    = 2,  /*!< I range 10 nA VMP3 / SP-300 series */
    KBIO_IRANGE_100nA   = 3,  /*!< I range 100 nA VMP3 / SP-300 series */
    KBIO_IRANGE_1uA     = 4,  /*!< I range 1 uA VMP3 / SP-300 series */
    KBIO_IRANGE_10uA    = 5,  /*!< I range 10 uA VMP3 / SP-300 series */
    KBIO_IRANGE_100uA   = 6,  /*!< I range 100 uA VMP3 / SP-300 series */
    KBIO_IRANGE_1mA     = 7,  /*!< I range 1 mA VMP3 / SP-300 series */
    KBIO_IRANGE_10mA    = 8,  /*!< I range 10 mA VMP3 / SP-300 series */
    KBIO_IRANGE_100mA   = 9,  /*!< I range 100 mA VMP3 / SP-300 series */
    KBIO_IRANGE_1A      = 10, /*!< I range 1 A VMP3 / SP-300 series */
    KBIO_IRANGE_BOOSTER = 11, /*!< Booster VMP3 / SP-300 series */
    KBIO_IRANGE_AUTO    = 12, /*!< Auto range VMP3 / SP-300 series */
    KBIO_IRANGE_10pA    = 13, /*!< IRANGE_100pA + Igain x10 */
    KBIO_IRANGE_1pA     = 14, /*!< IRANGE_100pA + Igain x100 */
} TIntensityRange_e;

/** Option error codes */
typedef enum
{
    KBIO_OPT_NOERR         = 0,   /*!< Option no error  */
    KBIO_OPT_CHANGE        = 1,   /*!< Option change  */
    KBIO_OPT_4A10V_ERR     = 100, /*!< Amplifier 4A10V error  */
    KBIO_OPT_4A10V_OVRTEMP = 101, /*!< Amplifier 4A10V overload temperature  */
    KBIO_OPT_4A10V_BADPOW  = 102, /*!< Amplifier 4A10V invalid power  */
    KBIO_OPT_4A10V_POWFAIL = 103, /*!< Amplifier 4A10V power fail  */
    KBIO_OPT_1A48V_ERR     = 200, /*!< Amplifier 1A48V error  */
    KBIO_OPT_1A48V_OVRTEMP = 201, /*!< Amplifier 1A48V overload temperature  */
    KBIO_OPT_1A48V_BADPOW  = 202, /*!< Amplifier 1A48V invalid power  */
    KBIO_OPT_1A48V_POWFAIL = 203, /*!< Amplifier 1A48V power fail  */
    KBIO_OPT_10A5V_ERR     = 300, /*!< Amplifier 10A5V error  */
    KBIO_OPT_10A5V_OVRTEMP = 301, /*!< Amplifier 10A5V overload temperature  */
    KBIO_OPT_10A5V_BADPOW  = 302, /*!< Amplifier 10A5V invalid power  */
    KBIO_OPT_10A5V_POWFAIL = 303, /*!< Amplifier 10A5V power fail  */
} TOptionError_e;

/** Voltage range */
typedef enum
{
    KBIO_ERANGE_2_5  = 0, /*!< +/- 2.5 V */
    KBIO_ERANGE_5    = 1, /*!< +/- 5 V */
    KBIO_ERANGE_10   = 2, /*!< +/- 10 V */
    KBIO_ERANGE_AUTO = 3  /*!< Auto range */
} TVoltageRange_e;

/** Bandwidth */
typedef enum
{
    KBIO_BW_1 = 1, /*!< Bandwidth #1 */
    KBIO_BW_2 = 2, /*!< Bandwidth #2 */
    KBIO_BW_3 = 3, /*!< Bandwidth #3 */
    KBIO_BW_4 = 4, /*!< Bandwidth #4 */
    KBIO_BW_5 = 5, /*!< Bandwidth #5 */
    KBIO_BW_6 = 6, /*!< Bandwidth #6 */
    KBIO_BW_7 = 7, /*!< Bandwidth #7 */
    KBIO_BW_8 = 8, /*!< Bandwidth #8 (only with SP-300 series) */
    KBIO_BW_9 = 9  /*!< Bandwidth #9 (only with SP-300 series) */
} TBandwidth_e;

/** E/I gain constants */
typedef enum
{
    KBIO_GAIN_1    = 0,
    KBIO_GAIN_10   = 1,
    KBIO_GAIN_100  = 2,
    KBIO_GAIN_1000 = 3
} TGain_e;

/** Electrode connection */
typedef enum
{
    KBIO_CONN_STD      = 0, /*!< Standard connection */
    KBIO_CONN_CETOGRND = 1, /*!< CE to ground connection */
    KBIO_CONN_WETOGRND = 2,
    KBIO_CONN_HV       = 3,
} TElectrodeConn_e;

/** Electrode Ground mode */
typedef enum
{
    KBIO_MODE_GROUNDED = 0, /*!< Grounded mode */
    KBIO_MODE_FLOATING = 1  /*!< floating mode */
} TElectrodeMode_e;

/** E/I filter constants */
typedef enum
{
    KBIO_FILTER_NONE  = 0,
    KBIO_FILTER_50KHZ = 1,
    KBIO_FILTER_1KHZ  = 2,
    KBIO_FILTER_5HZ   = 3,
} TFilterFreqCut_e;

/** Technique IDs enumeration */
typedef enum
{
    KBIO_TECHID_NONE            = 0,   /*!< None */
    KBIO_TECHID_OCV             = 100, /*!< Open Circuit Voltage (Rest) identifier */
    KBIO_TECHID_CA              = 101, /*!< Chrono-amperometry identifier */
    KBIO_TECHID_CP              = 102, /*!< Chrono-potentiometry identifier */
    KBIO_TECHID_CV              = 103, /*!< Cyclic Voltammetry identifier */
    KBIO_TECHID_PEIS            = 104, /*!< Potentio Electrochemical Impedance Spectroscopy identifier */
    KBIO_TECHID_POTPULSE        = 105, /*!< (unused) */
    KBIO_TECHID_GALPULSE        = 106, /*!< (unused) */
    KBIO_TECHID_GEIS            = 107, /*!< Galvano Electrochemical Impedance Spectroscopy identifier */
    KBIO_TECHID_STACKPEIS_SLAVE = 108, /*!< Potentio Electrochemical Impedance Spectroscopy on stack identifier */
    KBIO_TECHID_STACKPEIS       = 109, /*!< Potentio Electrochemical Impedance Spectroscopy on stack identifier */
    KBIO_TECHID_CPOWER          = 110, /*!< Constant Power identifier */
    KBIO_TECHID_CLOAD           = 111, /*!< Constant Load identifier */
    KBIO_TECHID_FCT             = 112, /*!< (unused) */
    KBIO_TECHID_SPEIS           = 113, /*!< Staircase Potentio Electrochemical Impedance Spectroscopy identifier */
    KBIO_TECHID_SGEIS           = 114, /*!< Staircase Galvano Electrochemical Impedance Spectroscopy identifier */
    KBIO_TECHID_STACKPDYN       = 115, /*!< Potentio dynamic on stack identifier */
    KBIO_TECHID_STACKPDYN_SLAVE = 116, /*!< Potentio dynamic on stack identifier */
    KBIO_TECHID_STACKGDYN       = 117, /*!< Galvano dynamic on stack identifier */
    KBIO_TECHID_STACKGEIS_SLAVE = 118, /*!< Galvano Electrochemical Impedance Spectroscopy on stack identifier */
    KBIO_TECHID_STACKGEIS       = 119, /*!< Galvano Electrochemical Impedance Spectroscopy on stack identifier */
    KBIO_TECHID_STACKGDYN_SLAVE = 120, /*!< Galvano dynamic on stack identifier */
    KBIO_TECHID_CPO             = 121, /*!< (unused) */
    KBIO_TECHID_CGA             = 122, /*!< (unused) */
    KBIO_TECHID_COKINE          = 123, /*!< (unused) */
    KBIO_TECHID_PDYN            = 124, /*!< Potentio dynamic identifier */
    KBIO_TECHID_GDYN            = 125, /*!< Galvano dynamic identifier */
    KBIO_TECHID_CVA             = 126, /*!< Cyclic Voltammetry Advanced identifier */
    KBIO_TECHID_DPV             = 127, /*!< Differential Pulse Voltammetry identifier */
    KBIO_TECHID_SWV             = 128, /*!< Square Wave Voltammetry identifier */
    KBIO_TECHID_NPV             = 129, /*!< Normal Pulse Voltammetry identifier */
    KBIO_TECHID_RNPV            = 130, /*!< Reverse Normal Pulse Voltammetry identifier */
    KBIO_TECHID_DNPV            = 131, /*!< Differential Normal Pulse Voltammetry identifier */
    KBIO_TECHID_DPA             = 132, /*!< Differential Pulse Amperometry identifier */
    KBIO_TECHID_EVT             = 133, /*!< Ecorr vs. time identifier */
    KBIO_TECHID_LP              = 134, /*!< Linear Polarization identifier */
    KBIO_TECHID_GC              = 135, /*!< Generalized corrosion identifier */
    KBIO_TECHID_CPP             = 136, /*!< Cyclic Potentiodynamic Polarization identifier */
    KBIO_TECHID_PDP             = 137, /*!< Potentiodynamic Pitting identifier */
    KBIO_TECHID_PSP             = 138, /*!< Potentiostatic Pitting identifier */
    KBIO_TECHID_ZRA             = 139, /*!< Zero Resistance Ammeter identifier */
    KBIO_TECHID_MIR             = 140, /*!< Manual IR identifier */
    KBIO_TECHID_PZIR            = 141, /*!< IR Determination with Potentiostatic Impedance identifier */
    KBIO_TECHID_GZIR            = 142, /*!< IR Determination with Galvanostatic Impedance identifier */
    KBIO_TECHID_LOOP            = 150, /*!< Loop (used for linked techniques) identifier */
    KBIO_TECHID_TO              = 151, /*!< Trigger Out identifier */
    KBIO_TECHID_TI              = 152, /*!< Trigger In identifier */
    KBIO_TECHID_TOS             = 153, /*!< Trigger Set identifier */
    KBIO_TECHID_CPLIMIT         = 155, /*!< Chrono-potentiometry with limits identifier */
    KBIO_TECHID_GDYNLIMIT       = 156, /*!< Galvano dynamic with limits identifier */
    KBIO_TECHID_CALIMIT         = 157, /*!< Chrono-amperometry with limits identifier */
    KBIO_TECHID_PDYNLIMIT       = 158, /*!< Potentio dynamic with limits identifier */
    KBIO_TECHID_LASV            = 159, /*!< Large amplitude sinusoidal voltammetry */
    KBIO_TECHID_MUXLOOP         = 160,
    KBIO_TECHID_CVCA            = 161,
    KBIO_TECHID_CVCA_SLAVE      = 162,
    KBIO_TECHID_CPCA            = 163,
    KBIO_TECHID_CPCA_SLAVE      = 164,
    KBIO_TECHID_CACA            = 165,
    KBIO_TECHID_CACA_SLAVE      = 166,
    KBIO_TECHID_MP              = 167, /*!< Modular Pulse */
    KBIO_TECHID_CASG            = 169, /*!< Constant amplitude sinusoidal micro galvano polarization */
    KBIO_TECHID_CASP            = 170, /*!< Constant amplitude sinusoidal micro potentio polarization */
    KBIO_TECHID_VASP            = 171,
    KBIO_TECHID_UCVANALOG       = 172,

    KBIO_TECHID_UNIPANEL = 200,

    KBIO_TECHID_OCVR = 500,
    KBIO_TECHID_CAR  = 501,
    KBIO_TECHID_CPR  = 502,

    KBIO_TECHID_ABS     = 1000,
    KBIO_TECHID_FLUO    = 1001,
    KBIO_TECHID_RABS    = 1002,
    KBIO_TECHID_RFLUO   = 1003,
    KBIO_TECHID_RDABS   = 1004,
    KBIO_TECHID_DABS    = 1005,
    KBIO_TECHID_ABSFLUO = 1006,
    KBIO_TECHID_RAFABS  = 1007,
    KBIO_TECHID_RAFFLUO = 1008,
} TTechniqueIdentifier_e;

/** Channel State */
typedef enum
{
    KBIO_STATE_STOP  = 0, /*!< Channel is stopped */
    KBIO_STATE_RUN   = 1, /*!< Channel is running */
    KBIO_STATE_PAUSE = 2  /*!< Channel is paused */

} TChannelState_e;

typedef enum
{
    KBIO_FLOAT_TYPE_TI   = 0, /*!< Channel uses Ti format floats */
    KBIO_FLOAT_TYPE_IEEE = 1  /*!< Channel uses IEEE format floats */
} TFloatFormat_e;

typedef enum
{
    KBIO_FPGA_VMP_0329 = 0xA500,
    KBIO_FPGA_VMP_0340 = 0xA600,
    KBIO_FPGA_VMP_0368 = 0xA700,

    KBIO_FPGA_VMP4_0368_02    = 0xA800,
    KBIO_FPGA_VMP4_0368_03    = 0xA880,
    KBIO_FPGA_VMP4_0395_fdp   = 0xA900,
    KBIO_FPGA_VMP4_0395_01    = 0xAA00,
    KBIO_FPGA_VMP4_0387_01    = 0xAB00,
    KBIO_FPGA_VMP4_0395_02    = 0xAC00,
    KBIO_FPGA_VMP4_0395_Opera = 0xAD00,
    KBIO_FPGA_VMP4_0395_DC300 = 0xAE00
} TFPGAType_e;

typedef enum
{
    KBIO_VMP3_Channel = 1,
    KBIO_VMP4_Channel = 2,
    KBIO_VMP5_Channel = 3
} TChannelType_e;

/** Parameter type */
typedef enum
{
    PARAM_INT32   = 0, /*!< Parameter type = int */
    PARAM_BOOLEAN = 1, /*!< Parameter type = boolean */
    PARAM_SINGLE  = 2  /*!< Parameter type = single */
} TParamType_e;

/** ECLib Error codes */
typedef enum
{
    ERR_NOERROR = 0, /*!< No Error */

    /* General error codes */
    ERR_GEN_NOTCONNECTED          = -1,  /*!< no instrument connected */
    ERR_GEN_CONNECTIONINPROGRESS  = -2,  /*!< connection in progress */
    ERR_GEN_CHANNELNOTPLUGGED     = -3,  /*!< selected channel(s) unplugged */
    ERR_GEN_INVALIDPARAMETERS     = -4,  /*!< invalid function parameters */
    ERR_GEN_FILENOTEXISTS         = -5,  /*!< selected file does not exist */
    ERR_GEN_FUNCTIONFAILED        = -6,  /*!< function failed */
    ERR_GEN_NOCHANNELELECTED      = -7,  /*!< no channel selected */
    ERR_GEN_INVALIDCONF           = -8,  /*!< invalid instrument configuration */
    ERR_GEN_ECLAB_LOADED          = -9,  /*!< EC-Lab firmware loaded on the instrument */
    ERR_GEN_LIBNOTCORRECTLYLOADED = -10, /*!< library not correctly loaded in memory */
    ERR_GEN_USBLIBRARYERROR       = -11, /*!< USB library not correctly loaded in memory */
    ERR_GEN_FUNCTIONINPROGRESS    = -12, /*!< function of the library already in progress */
    ERR_GEN_CHANNEL_RUNNING       = -13, /*!< selected channel(s) already used */
    ERR_GEN_DEVICE_NOTALLOWED     = -14, /*!< device not allowed */
    ERR_GEN_UPDATEPARAMETERS      = -15, /*!< Invalid update function parameters */

    /* Instrument error codes */
    ERR_INSTR_VMEERROR        = -101, /*!< internal instrument communication failed */
    ERR_INSTR_TOOMANYDATA     = -102, /*!< too many data to transfer from the instrument (device error) */
    ERR_INSTR_RESPNOTPOSSIBLE = -103, /*!< selected channel(s) unplugged (device error) */
    ERR_INSTR_RESPERROR       = -104, /*!< instrument response error */
    ERR_INSTR_MSGSIZEERROR    = -105, /*!< invalid message size */

    /* Communication error codes */
    ERR_COMM_COMMFAILED         = -200, /*!< communication failed with the instrument */
    ERR_COMM_CONNECTIONFAILED   = -201, /*!< cannot establish connection with the instrument */
    ERR_COMM_WAITINGACK         = -202, /*!< waiting for the instrument response */
    ERR_COMM_INVALIDIPADDRESS   = -203, /*!< invalid IP address */
    ERR_COMM_ALLOCMEMFAILED     = -204, /*!< cannot allocate memory in the instrument */
    ERR_COMM_LOADFIRMWAREFAILED = -205, /*!< cannot load firmware into selected channel(s) */
    ERR_COMM_INCOMPATIBLESERVER = -206, /*!< communication firmware not compatible with the library */
    ERR_COMM_MAXCONNREACHED     = -207, /*!< maximum number of allowed connections reached */

    /* Firmware error codes */
    ERR_FIRM_FIRMFILENOTEXISTS    = -300, /*!< cannot find kernel.bin file */
    ERR_FIRM_FIRMFILEACCESSFAILED = -301, /*!< cannot read kernel.bin file */
    ERR_FIRM_FIRMINVALIDFILE      = -302, /*!< invalid kernel.bin file */
    ERR_FIRM_FIRMLOADINGFAILED    = -303, /*!< cannot load kernel.bin on the selected channel(s) */
    ERR_FIRM_XILFILENOTEXISTS     = -304, /*!< cannot find x100_01.txt file */
    ERR_FIRM_XILFILEACCESSFAILED  = -305, /*!< cannot read x100_01.txt file */
    ERR_FIRM_XILINVALIDFILE       = -306, /*!< invalid x100_01.txt file */
    ERR_FIRM_XILLOADINGFAILED     = -307, /*!< cannot load x100_01.txt file on the selected channel(s) */
    ERR_FIRM_FIRMWARENOTLOADED    = -308, /*!< no firmware loaded on the selected channel(s) */
    ERR_FIRM_FIRMWAREINCOMPATIBLE = -309, /*!< loaded firmware not compatible with the library */

    /* Technique error codes */
    ERR_TECH_ECCFILENOTEXISTS    = -400, /*!< cannot find the selected ECC file */
    ERR_TECH_INCOMPATIBLEECC     = -401, /*!< ECC file not compatible with the channel firmware */
    ERR_TECH_ECCFILECORRUPTED    = -402, /*!< ECC file corrupted */
    ERR_TECH_LOADTECHNIQUEFAILED = -403, /*!< cannot load the ECC file */
    ERR_TECH_DATACORRUPTED       = -404, /*!< data returned by the instrument are corrupted */
    ERR_TECH_MEMFULL             = -405  /*!< cannot load techniques: full memory */
} TErrorCodes_e;

/** BLFind error codes */
typedef enum
{
    BLFIND_ERR_UNKNOWN           = -1, /*!< unknown error */
    BLFIND_ERR_INVALID_PARAMETER = -2, /*!< invalid function parameters */

    BLFIND_ERR_ACK_TIMEOUT = -10, /*!< instrument response timeout */
    BLFIND_ERR_EXP_RUNNING = -11, /*!< experiment is running on instrument */
    BLFIND_ERR_CMD_FAILED  = -12, /*!< instrument do not execute command */

    BLFIND_ERR_FIND_FAILED  = -20, /*!< find failed */
    BLFIND_ERR_SOCKET_WRITE = -21, /*!< cannot write the request of the descriptions of ethernet instruments */
    BLFIND_ERR_SOCKET_READ  = -22, /*!< cannot read descriptions of ethernet instrument */

    BLFIND_ERR_CFG_MODIFY_FAILED = -30, /*!< set TCP/IP parameters failed */
    BLFIND_ERR_READ_PARAM_FAILED = -31, /*!< deserialization of TCP/IP parameters failed */
    BLFIND_ERR_EMPTY_PARAM       = -32, /*!< not any TCP/IP parameters in serialization */
    BLFIND_ERR_IP_FORMAT         = -33, /*!< invalid format of IP address */
    BLFIND_ERR_NM_FORMAT         = -34, /*!< invalid format of netmask address */
    BLFIND_ERR_GW_FORMAT         = -35, /*!< invalid format of gateway address */
    BLFIND_ERR_IP_NOT_FOUND      = -38, /*!< instrument to modify not found */
    BLFIND_ERR_IP_ALREADYEXIST   = -39  /*!< new IP address in TCP/IP parameters already exists */
} TBLFindError_e;

    #pragma pack( pop )
/** @} */

#endif /* _BLSTRUCTS_H_ */
