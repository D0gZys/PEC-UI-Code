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
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <BLFunctions.h>
#include <BLStructs.h>

#define ADDRESS     "10.100.19.1"
#define CHANNEL     1
#define CHANNEL_ID  CHANNEL - 1
#define TIMEOUT     5
#define MAX_CHANNEL 16

TEccParams_t* get_params()
{
    // Construct ECCParams
    auto params     = new TEccParams_t(); /* note the "s" in the type... */
    params->len     = 15;
    params->pParams = new TEccParam_t[params->len];

    int status = ERR_NOERROR;
    // Step #1
    status |= BL_DefineSglParameter( "Voltage_step", 1.0, 0,
                                     &params->pParams[0] ); // V0 (V)
    status |= BL_DefineBoolParameter( "vs_initial", false, 0,
                                      &params->pParams[1] ); // vs. init
    status |= BL_DefineSglParameter( "Duration_step", 2.0, 0,
                                     &params->pParams[2] ); // Step duration (s)
    // Step #1
    status |= BL_DefineSglParameter( "Voltage_step", 2.0, 1,
                                     &params->pParams[3] ); // V1 (V)
    status |= BL_DefineBoolParameter( "vs_initial", false, 1,
                                      &params->pParams[4] ); // scan to E1 s. init
    status |= BL_DefineSglParameter( "Duration_step", 2.0, 1,
                                     &params->pParams[5] ); // Step duration (s
    // Step #2
    status |= BL_DefineSglParameter( "Voltage_step", 3.0, 2,
                                     &params->pParams[6] ); // V2 (V)
    status |= BL_DefineBoolParameter( "vs_initial", false, 2,
                                      &params->pParams[7] ); // vs. init
    status |= BL_DefineSglParameter( "Duration_step", 2.0, 2,
                                     &params->pParams[8] ); // Step duration (s)
    // Steps max
    status |= BL_DefineIntParameter( "Step_number", 2, 0,
                                     &params->pParams[9] ); // step number
    // others
    status |= BL_DefineIntParameter( "N_Cycles", 1, 0,
                                     &params->pParams[10] ); // repeat Nc time
    status |= BL_DefineSglParameter( "Record_every_dI", 0.1, 0,
                                     &params->pParams[11] ); // record every dI (V)
    status |= BL_DefineSglParameter( "Record_every_dT", 0.1, 0,
                                     &params->pParams[12] ); // or every dT (s)
    status |= BL_DefineIntParameter( "I_Range", KBIO_IRANGE_AUTO, 0,
                                     &params->pParams[13] ); // I Range
    status |= BL_DefineIntParameter( "Bandwidth", KBIO_BW_7, 0,
                                     &params->pParams[14] ); // bandwidth

    if( status != ERR_NOERROR )
    {
        std::cout << "Error during parameter definition" << std::endl;
        exit( 1 );
    }

    return std::move( params );
}

std::string print_header( TDataInfos_t info )
{
    return "Time (S),Ewe (V),Iwe (A),Cycle (N)";
}

std::string parse_data( TDataBuffer_t buffer, TDataInfos_t infos, int offset, uint32_t board_type )
{
    std::string result = "";
    float       measure;
    for( int j = 2; j < infos.NbCols - 1; j++ )
    {
        if( BL_ConvertChannelNumericIntoSingle( buffer.data[offset + j], &measure, board_type ) != ERR_NOERROR )
        {
            std::cout << "Data parsing error" << std::endl;
            exit( 1 );
        }
        std::stringstream ss;
        ss << std::fixed << std::setprecision( 20 ) << measure;
        result += "," + ss.str();
    }
    result += "," + std::to_string( buffer.data[infos.NbCols - 1] );
    return result;
}

void process_data( int32_t connection_id )
{
    TDataBuffer_t    buffer;
    TDataInfos_t     infos;
    TCurrentValues_t curr;
    bool             Running     = true;
    int              SamplesRead = 0;
    std::ofstream    outputFile( "ca_example.csv" );
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
            std::cout << "Unkown board type" << std::endl;
            exit( 1 );
    }
}

std::string get_technique_files( uint32_t board_type )
{
    switch( board_type )
    {
        case 1:
            return "ca.ecc";
        case 2:
            return "ca4.ecc";
        case 3:
            return "ca5.ecc";
        default:
            std::cout << "Unkown board type" << std::endl;
            exit( 1 );
    }
}

int main( int argc, char** argv )
{
    int32_t        connection_id;
    TDeviceInfos_t device_infos;

    // Connect to instrument
    std::cout << "Connecting to instrument " << ADDRESS << std::endl;
    if( BL_Connect( ADDRESS, TIMEOUT, &connection_id, &device_infos ) != ERR_NOERROR )
    {
        std::cout << "Can't connect to instrument." << std::endl;
        exit( 1 );
    }

    // Get channel board type
    uint32_t board_type = 0;
    std::cout << "Using channel " << CHANNEL << std::endl;
    if( BL_GetChannelBoardType( connection_id, CHANNEL_ID, &board_type ) != ERR_NOERROR )
    {
        std::cout << "Can't get channel board type." << std::endl;
        exit( 1 );
    }

    // Get firmware files based on board type
    std::string firmware_file;
    std::string fpga_file;
    get_firmware_files( board_type, firmware_file, fpga_file );

    // Load firmware
    uint8_t channels[MAX_CHANNEL] = { 0 };
    int     results[MAX_CHANNEL]  = { 0 };
    channels[CHANNEL_ID]          = 1;

    std::cout << "Loading firmware " << firmware_file << " on instrument " << ADDRESS << ", channel " << CHANNEL << std::endl;

    if( BL_LoadFirmware( connection_id, channels, results, MAX_CHANNEL, true, true, firmware_file.c_str(), fpga_file.c_str() ) !=
        ERR_NOERROR )
    {
        std::cout << "Load formware failed" << std::endl;
        exit( 1 );
    }

    // Load technique
    std::string technique = get_technique_files( board_type );
    std::cout << "Loading technique " << technique << " on channel " << CHANNEL << std::endl;
    auto params = get_params();
    if( BL_LoadTechnique( connection_id, CHANNEL_ID, technique.c_str(), *params, true, true, false ) != ERR_NOERROR )
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

    return EXIT_SUCCESS;
}
