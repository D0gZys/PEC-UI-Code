// -----------------------------------------------------------------
// This document is a part of the BioLogic OEM Package and is
// protected by the terms of the OEM Package licence as well as
// other intellectual property rights owned by BioLogic SAS.
// This document may only be used for non-commercial purposes
// such as for the integration of BioLogic equipment to larger
// technical solutions manufactured  and/or delivered to end-users.
// -----------------------------------------------------------------
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <BLFunctions.h>
#include <BLStructs.h>

#define BUFFER_SIZE 64
#define ADDRESS     "10.100.19.1"
#define CHANNEL     1
#define CHANNEL_ID  CHANNEL - 1
#define TIMEOUT     5
#define TECHNIQUE   "ocv4.ecc" // Note: use ocv.ecc for VMP3 serie and ocv5.ecc for VMP300-P

TEccParams_t* get_params()
{
    // Construct ECCParams
    auto params     = new TEccParams_t(); /* note the "s" in the type... */
    params->len     = 4;
    params->pParams = new TEccParam_t[params->len];
    BL_DefineSglParameter( "Rest_time_T", 10.0, 0, &params->pParams[0] );
    BL_DefineSglParameter( "Record_every_dE", 10.0, 0, &params->pParams[1] );
    BL_DefineSglParameter( "Record_every_dT", 0.1, 0, &params->pParams[2] );
    BL_DefineIntParameter( "E_Range", 2, 0, &params->pParams[3] ); // E_RANGE_10V
    return std::move( params );
}

std::string print_header( TDataInfos_t info )
{
    if( info.NbCols == 3 )
    {
        return "time (s),Ewe (V)";
    }
    if( info.NbCols == 4 )
    {
        return "time (s),Ewe (V),Ece (V)";
    }
    return "";
}

std::string parse_data( TDataBuffer_t buffer, TDataInfos_t infos, int offset, uint32_t board_type )
{
    std::string result = "";
    float       measure;
    for( int j = 2; j < infos.NbCols; j++ )
    {
        if( BL_ConvertChannelNumericIntoSingle( buffer.data[offset + j], &measure, board_type ) != ERR_NOERROR )
        {
            std::cout << "Parse data error" << std::endl;
            exit( 1 );
        }
        result += "," + std::to_string( measure );
    }
    return result;
}

void process_data( int32_t connection_id )
{
    TDataBuffer_t    buffer;
    TDataInfos_t     infos;
    TCurrentValues_t curr;
    bool             Running     = true;
    int              SamplesRead = 0;
    std::ofstream    outputFile( "ocv_example.csv" );
    if( outputFile.is_open() )
    {
        uint32_t board_type;
        BL_GetChannelBoardType( connection_id, CHANNEL_ID, &board_type );

        bool header_printed = false;
        while( Running )
        {
            if( BL_GetData( connection_id, CHANNEL_ID, &buffer, &infos, &curr ) != ERR_NOERROR )
            {
                std::cout << "Failed to Get Data" << std::endl;
                exit( 1 );
            }

            if( !header_printed )
            {
                outputFile << print_header( infos ) << std::endl;
                header_printed = true;
            }
            double start_time = infos.StartTime;
            double timebase   = (double)curr.TimeBase;

            for( int i = 0; i < infos.NbRows; i++ )
            {
                int    offset = i * infos.NbCols;
                double duration_since_start;
                BL_ConvertTimeChannelNumericIntoSeconds( &buffer.data[offset], &duration_since_start, timebase, 0 );
                double time = start_time + duration_since_start;
                outputFile << std::setprecision( std::numeric_limits< float >::max_digits10 ) << time
                           << parse_data( buffer, infos, offset, board_type ) << std::endl;
                std::cout << ".";
            }

            if( curr.State == KBIO_STATE_STOP )
            {
                Running = false;
            }
        }
        outputFile.close();
    }
}

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#include <BLFunctions.h>
#include <BLStructs.h>

#define ADDRESS     "10.100.19.1"
#define TIMEOUT     5
#define MAX_CHANNEL 16

void print_channels_plugged( uint8 channels_plugged[MAX_CHANNEL] )
{
    std::cout << "Channels plugged  = ";
    std::string channel_list = "";
    for( int i = 0; i < MAX_CHANNEL; ++i )
    {
        if( channels_plugged[i] != 0 )
        {
            if( channel_list != "" )
            {
                channel_list += ", ";
            }
            channel_list += std::to_string( i + 1 );
        }
    }
    std::cout << channel_list << std::endl;
}

void print_device_info( const TDeviceInfos_t& device_infos )
{
    std::cout << "DeviceCode        = " << device_infos.DeviceCode << std::endl;
    std::cout << "RAMSize           = " << device_infos.RAMSize << std::endl;
    std::cout << "CPU               = " << device_infos.CPU << std::endl;
    std::cout << "NumberOfChannels  = " << device_infos.NumberOfChannels << std::endl;
    std::cout << "NumberOfSlots     = " << device_infos.NumberOfSlots << std::endl;
    std::cout << "FirmwareVersion   = " << device_infos.FirmwareVersion << std::endl;
    std::cout << "FirmwareDate_yyyy = " << device_infos.FirmwareDate_yyyy << std::endl;
    std::cout << "FirmwareDate_mm   = " << device_infos.FirmwareDate_mm << std::endl;
    std::cout << "FirmwareDate_dd   = " << device_infos.FirmwareDate_dd << std::endl;
    std::cout << "HTdisplayOn       = " << device_infos.HTdisplayOn << std::endl;
    std::cout << "NbOfConnectedPC   = " << device_infos.NbOfConnectedPC << std::endl;
}

void print_channel_info( const TChannelInfos_t& channel_info )
{
    std::cout << "Channel             = " << channel_info.Channel << std::endl;
    std::cout << "  BoardVersion      = " << channel_info.BoardVersion << std::endl;
    std::cout << "  BoardSerialNumber = " << channel_info.BoardSerialNumber << std::endl;
    std::cout << "  FirmwareCode      = " << channel_info.FirmwareCode << std::endl;
    std::cout << "  FirmwareVersion   = " << channel_info.FirmwareVersion << std::endl;
    std::cout << "  XilinxVersion     = " << channel_info.XilinxVersion << std::endl;
    std::cout << "  AmpCode           = " << channel_info.AmpCode << std::endl;
    std::cout << "  NbAmps            = " << channel_info.NbAmps << std::endl;
    std::cout << "  Lcboard           = " << channel_info.Lcboard << std::endl;
    std::cout << "  Zboard            = " << channel_info.Zboard << std::endl;
    std::cout << "  MemSize           = " << channel_info.MemSize << std::endl;
    std::cout << "  MemFilled         = " << channel_info.MemFilled << std::endl;
    std::cout << "  State             = " << channel_info.State << std::endl;
    std::cout << "  MaxIRange         = " << channel_info.MaxIRange << std::endl;
    std::cout << "  MinIRange         = " << channel_info.MinIRange << std::endl;
    std::cout << "  MaxBandwidth      = " << channel_info.MaxBandwidth << std::endl;
    std::cout << "  NbOfTechniques    = " << channel_info.NbOfTechniques << std::endl;
}

void get_firmware_files( uint32_t board_type, std::string& firmware_file, std::string& fpga_file )
{
    switch( board_type )
    {
        case 1:
            firmware_file = "kernel.bin";
            fpga_file     = "Vmp_ii_0437_a6.xlx";
            break;
        case 2:
            firmware_file = "kernel4.bin";
            fpga_file     = "vmp_iv_0395_aa.xlx";
            break;
        case 3:
            firmware_file = "kernel5.bin";
            fpga_file     = "";
            break;
        default:
            assert( false && "Unkown board type" );
    }
}

int main( int argc, char** argv )
{
    int32_t        connection_id;
    TDeviceInfos_t device_infos;

    try
    {
        // Connect to instrument
        std::cout << "Connecting to instrument " << ADDRESS << std::endl;
        if( BL_Connect( ADDRESS, TIMEOUT, &connection_id, &device_infos ) != ERR_NOERROR )
        {
            std::cout << "Can't connect to instrument." << std::endl;
            exit( 1 );
        }

        // Get channel board type
        uint32_t board_type = 0;
        assert( BL_GetChannelBoardType( connection_id, CHANNEL_ID, &board_type ) == ERR_NOERROR && "Can't get channel board type." );

        // Get firmware files based on board type
        std::string firmware_file;
        std::string fpga_file;
        get_firmware_files( board_type, firmware_file, fpga_file );

        // Load firmware
        uint8_t channels[MAX_CHANNEL] = { 0 };
        int     results[MAX_CHANNEL]  = { 0 };
        channels[CHANNEL_ID]          = 1;

        assert( BL_LoadFirmware( connection_id, channels, results, MAX_CHANNEL, true, true, firmware_file.c_str(), fpga_file.c_str() ) ==
                    ERR_NOERROR &&
                "Load formware failed" );

        // Load technique
        std::cout << "Loading technique " << TECHNIQUE << " on channel " << CHANNEL << std::endl;
        auto params = get_params();
        if( BL_LoadTechnique( connection_id, CHANNEL_ID, TECHNIQUE, *params, true, true, false ) != ERR_NOERROR )
        {
            std::cout << "Failed to load technique" << std::endl;
            exit( 1 );
        }
        delete params;

        // Start technique
        std::cout << "Starting experiment" << std::endl;
        if( BL_StartChannel( connection_id, CHANNEL_ID ) != ERR_NOERROR )
        {
            std::cout << "Failed to start channel" << std::endl;
            exit( 1 );
        }

        // Write data into a file
        std::cout << "Reading data ";
        process_data( connection_id );
        std::cout << std::endl;

        // Disconnect
        std::cout << "Disconnecting from instrument" << std::endl;
        BL_Disconnect( connection_id );
    }
    catch( const std::exception& e )
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
