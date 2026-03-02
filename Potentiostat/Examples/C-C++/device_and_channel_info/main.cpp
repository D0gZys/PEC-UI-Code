// -----------------------------------------------------------------
// This document is a part of the BioLogic OEM Package and is
// protected by the terms of the OEM Package licence as well as
// other intellectual property rights owned by BioLogic SAS.
// This document may only be used for non-commercial purposes
// such as for the integration of BioLogic equipment to larger
// technical solutions manufactured  and/or delivered to end-users.
// -----------------------------------------------------------------
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
            std::cout << "Unkown board type" << std::endl;
            exit( 1 );
    }
}

int main( int argc, char** argv )
{
    int32_t        connection_id = 0;
    TDeviceInfos_t device_infos;
    memset( &device_infos, 0, sizeof( TDeviceInfos_t ) );

    if( BL_Connect( ADDRESS, TIMEOUT, &connection_id, &device_infos ) != ERR_NOERROR )
    {
        std::cout << "Can't connect to instrument." << std::endl;
        exit( 1 );
    }

    std::cout << "# DEVICE INFO" << std::endl;
    std::cout << std::endl;
    print_device_info( device_infos );

    uint8 channels_plugged[MAX_CHANNEL];
    BL_GetChannelsPlugged( connection_id, channels_plugged, MAX_CHANNEL );
    print_channels_plugged( channels_plugged );

    std::cout << std::endl;
    std::cout << std::endl;

    std::cout << "# CHANNEL INFO" << std::endl;
    std::cout << std::endl;

    for( uint8 channel = 0; channel < MAX_CHANNEL; ++channel )
    {
        if( channels_plugged[channel] != 0 )
        {
            // Get channel board type
            uint32_t board_type = 0;
            if( BL_GetChannelBoardType( connection_id, channel, &board_type ) != ERR_NOERROR )
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
            channels[channel]             = 1;

            if( BL_LoadFirmware( connection_id, channels, results, MAX_CHANNEL, true, true, firmware_file.c_str(), fpga_file.c_str() ) !=
                ERR_NOERROR )
            {
                std::cout << "Load formware failed" << std::endl;
                exit( 1 );
            }
            // Get channel info and print it
            TChannelInfos_t channel_info;
            memset( &channel_info, 0, sizeof( TChannelInfos_t ) );
            BL_GetChannelInfos( connection_id, channel, &channel_info );
            print_channel_info( channel_info );
        }
    }

    std::cout << "Channel 2 is " << ( BL_IsChannelPlugged( connection_id, 1 ) ? "" : "dis" ) << "connected" << std::endl;
    std::cout << "Channel 3 is " << ( BL_IsChannelPlugged( connection_id, 2 ) ? "" : "dis" ) << "connected" << std::endl;

    BL_Disconnect( connection_id );
    return 0;
}
