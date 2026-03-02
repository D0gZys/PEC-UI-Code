#####################################################################
# This document is a part of the BioLogic OEM Package and is
# protected by the terms of the OEM Package licence as well as
# other intellectual property rights owned by BioLogic SAS.
# This document may only be used for non-commercial purposes
# such as for the integration of BioLogic equipment to larger
# technical solutions manufactured  and/or delivered to end-users.
#####################################################################
""" Bio-Logic OEM package python API.

Script shown as an example of how to interface with a Biologic instrument
in Python using the EC-Lab OEM Package library.

The script uses parameters which are provided below.

"""

import os
import sys

import kbio.kbio_types as KBIO
from kbio.c_utils import c_is_64b
from kbio.kbio_api import KBIO_api
from kbio.utils import exception_brief

# ------------------------------------------------------------------------------#

# Test parameters, to be adjusted

# address = "USB0"
address = "10.100.19.1"
channel = 1
timeout = 10

verbosity = 3
load_firmware = True

binary_path = os.environ.get("ECLIB_DIR", f"C:{os.sep}EC-Lab Development Package{os.sep}lib")

# ==============================================================================#

# Helper functions


def newline():
    print()


def print_exception(e):
    print(f"{exception_brief(e, verbosity)}")


def print_messages(ch):
    while True:
        msg = api.GetMessage(id_, ch)
        if not msg:
            break
        print(msg)


def print_USB_info(index):
    """Print device information at USB index, if any."""
    try:
        info = api.GetUSBDeviceInfos(index)
        print(f"> USB{index} info : {info}")
    except Exception as e:
        print(f"> USB{index} info : {e}")


# determine library file according to Python version (32b/64b)

newline()

if c_is_64b:
    print("> 64b application")
    DLL_file = "EClib64.dll"
else:
    print("> 32b application")
    DLL_file = "EClib.dll"

DLL_path = f"{binary_path}{os.sep}{DLL_file}"

# ==============================================================================#

"""

Example main code : excercise various OEM package API calls.

  * open the DLL,
  * display its version,
  * display connected USB devices info if any,
  * connect to the device (whose address is defined above),
  * show relevant device info,
  * test whether the connection is working,
  * enumerate the channels available for that device,
  * retrieve and show the channel info (for the channel defined above),
  * optionally load the firmware if required,
  * re-display the channel info (might have changed if firmware was not loaded before),
  * display communication speed indication,
  * display and modify the instrument hardware configuration,
  * disconnect from the target,
  * show how to translate an error code to a descriptive text,
  * demonstrate that an exception is raised in case of error
   (here calling a function after being disconnected)

Note: for each call to the DLL, the base API function is shown in a comment.

"""


try:
    # API initialize
    api = KBIO_api(DLL_path)

    # BL_GetLibVersion
    version = api.GetLibVersion()
    print(f"> EcLib version: {version}")
    newline()

    # BL_GetUSBdeviceinfos
    print_USB_info(0)
    print_USB_info(1)
    newline()

    # BL_Connect
    id_, device_info = api.Connect(address, timeout)
    print(f"> device[{address}] info :")
    print(device_info)
    newline()

    # BL_TestConnection
    ok = "OK" if api.TestConnection(id_) else "not OK"
    print(f"> device[{address}] connection : {ok}")
    newline()

    # BL_GetChannelsPlugged
    # .. GetChannelsPlugged is a generator, expand into a set
    channels = {*api.GetChannelsPlugged(id_)}
    print(f"> device[{address}] channels : {channels}")
    newline()

    # test whether the configured channel exists
    if channel not in channels:
        print(f"Configured channel {channel} does not belong to device channels {channels}")
        sys.exit(-1)

    # BL_GetChannelInfos
    channel_info = api.GetChannelInfo(id_, channel)
    print(f"> Channel {channel} info :")
    print(channel_info)
    newline()

    # based on board_type, determine firmware filenames
    board_type = api.GetChannelBoardType(id_, channel)
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            firmware_path = "kernel.bin"
            fpga_path = "Vmp_ii_0437_a6.xlx"
        case KBIO.BOARD_TYPE.PREMIUM.value:
            firmware_path = "kernel4.bin"
            fpga_path = "vmp_iv_0395_aa.xlx"
        case KBIO.BOARD_TYPE.DIGICORE.value:
            firmware_path = "kernel.bin"
            fpga_path = ""
        case _:
            print(f"> Board type detection failed ({board_type})")
            sys.exit(-1)

    # Load firmware
    print(f"> Loading {firmware_path} ...")
    # create a map from channel set
    channel_map = api.channel_map({channel})
    # BL_LoadFirmware
    api.LoadFirmware(id_, channel_map, firmware=firmware_path, fpga=fpga_path, force=load_firmware)
    print("> ... firmware loaded")
    newline()

    # re-display info, as loading firmware provides more
    print(f"> Channel {channel} info :")
    # BL_GetChannelInfos
    info = api.GetChannelInfo(id_, channel)
    print(info)
    newline()

    # BL_TestCommSpeed
    rcvt_speed, firmware_speed = api.TestCommSpeed(id_, channel)
    speeds = {"rcvt": rcvt_speed, "firmware": firmware_speed}
    print(f"> device[{address}] speeds :")
    print(speeds)
    newline()

    # the following functions are ony valid for a VMP300
    if board_type == KBIO.BOARD_TYPE.PREMIUM.value:
        # BL_GetHardConf
        hw_conf = api.GetHardConf(id_, channel)
        hw_conf = {"cnx": hw_conf.connection, "mode": hw_conf.mode}
        print("> current hardware configuration :")
        print(hw_conf)

        # BL_SetHardConf
        cnx = KBIO.HW_CNX.WE_TO_GND
        mode = KBIO.HW_MODE.FLOATING
        api.SetHardConf(id_, channel, cnx.value, mode.value)

        # BL_GetHardConf
        hw_conf = api.GetHardConf(id_, channel)
        hw_conf = {"cnx": hw_conf.connection, "mode": hw_conf.mode}
        print("> new hardware configuration :")
        print(hw_conf)
        newline()

        # return to a standard configuration
        cnx = KBIO.HW_CNX.STANDARD
        mode = KBIO.HW_MODE.GROUNDED
        api.SetHardConf(id_, channel, cnx.value, mode.value)

    # BL_Disconnect
    api.Disconnect(id_)
    print(f"> disconnected from device[{address}]")
    newline()

    # BL_GetErrorMsg
    err_code = -202
    err_msg = api.GetErrorMsg(err_code).strip("\0")
    print(f"> error[{err_code}] description : '{err_msg}'")
    newline()

    # id_ is no longer valid, its use will trigger an exception
    print("> an exception will be raised :")
    api.TestConnection(id_)


except Exception as e:
    print_exception(e)

# ==============================================================================#
