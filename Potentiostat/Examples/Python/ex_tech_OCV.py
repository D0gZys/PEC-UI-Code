#####################################################################
# This document is a part of the BioLogic OEM Package and is
# protected by the terms of the OEM Package licence as well as
# other intellectual property rights owned by BioLogic SAS.
# This document may only be used for non-commercial purposes
# such as for the integration of BioLogic equipment to larger
# technical solutions manufactured  and/or delivered to end-users.
#####################################################################
""" Bio-Logic OEM package python API.

Script shown as an example of how to run an experiment with a Biologic instrument
using the EC-Lab OEM Package library.

The script uses parameters which are provided below.

"""

import os
import sys
import time

import kbio.kbio_types as KBIO
from kbio.c_utils import c_is_64b
from kbio.kbio_api import KBIO_api
from kbio.kbio_tech import ECC_parm
from kbio.kbio_tech import get_experiment_data
from kbio.kbio_tech import get_info_data
from kbio.kbio_tech import make_ecc_parm
from kbio.kbio_tech import make_ecc_parms
from kbio.utils import exception_brief

# ------------------------------------------------------------------------------#

# Test parameters, to be adjusted

verbosity = 1

# address = "USB0"
address = "10.100.19.1"
channel = 1

binary_path = os.environ.get("ECLIB_DIR", f"C:{os.sep}EC-Lab Development Package{os.sep}lib")

force_load_firmware = True

# OCV parameter values
ocv3_tech_file = "ocv.ecc"
ocv4_tech_file = "ocv4.ecc"
ocv5_tech_file = "ocv5.ecc"

duration = 10.0  # seconds
record_dt = 0.1  # seconds
e_range = "E_RANGE_10V"

# dictionary of OCV parameters (non exhaustive)

OCV_parms = {
    "duration": ECC_parm("Rest_time_T", float),
    "record_dt": ECC_parm("Record_every_dT", float),
    "record_dE": ECC_parm("Record_every_dE", float),
    "E_range": ECC_parm("E_Range", int),
    "timebase": ECC_parm("tb", int),
}

# ==============================================================================#

# helper functions


def newline():
    print()


# determine library file according to Python version (32b/64b)

if c_is_64b:
    DLL_file = "EClib64.dll"
else:
    DLL_file = "EClib.dll"

DLL_path = f"{binary_path}{os.sep}{DLL_file}"

# ==============================================================================#

"""

Example main :

  * open the DLL,
  * connect to the device using its address,
  * retrieve the device channel info,
  * test whether the proper firmware is running,
  * create an OCV parameter list (a subset of all possible parameters),
  * load the OCV technique into the channel,
  * start the technique,
  * in a loop :
      * retrieve and display experiment data,
      * stop when channel reports it is no longer running

Note: for each call to the DLL, the base API function is shown in a comment.

"""

try:
    newline()

    # API initialize
    api = KBIO_api(DLL_path)

    # BL_GetLibVersion
    version = api.GetLibVersion()
    print(f"> EcLib version: {version}")
    newline()

    # BL_Connect
    id_, device_info = api.Connect(address)
    print(f"> device[{address}] info :")
    print(device_info)
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
            print("> Board type detection failed")
            sys.exit(-1)

    # Load firmware
    print(f"> Loading {firmware_path} ...")
    # create a map from channel set
    channel_map = api.channel_map({channel})
    # BL_LoadFirmware
    api.LoadFirmware(id_, channel_map, firmware=firmware_path, fpga=fpga_path, force=force_load_firmware)
    print("> ... firmware loaded")
    newline()

    # BL_GetChannelInfos
    channel_info = api.GetChannelInfo(id_, channel)
    print(f"> Channel {channel} info :")
    print(channel_info)
    newline()

    if not channel_info.is_kernel_loaded:
        print("> kernel must be loaded in order to run the experiment")
        sys.exit(-1)

    # pick the correct ecc file based on the instrument family
    # pick the correct ecc file based on the instrument family
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            tech_file = ocv3_tech_file
        case KBIO.BOARD_TYPE.PREMIUM.value:
            tech_file = ocv4_tech_file
        case KBIO.BOARD_TYPE.DIGICORE.value:
            tech_file = ocv5_tech_file
        case _:
            print("> Board type detection failed")
            sys.exit(-1)

    # BL_Define<xxx>Parameter
    p_duration = make_ecc_parm(api, OCV_parms["duration"], duration)
    p_record = make_ecc_parm(api, OCV_parms["record_dt"], record_dt)
    p_erange = make_ecc_parm(api, OCV_parms["E_range"], KBIO.E_RANGE[e_range].value)
    ecc_parms = make_ecc_parms(api, p_duration, p_record, p_erange)

    # BL_LoadTechnique
    api.LoadTechnique(id_, channel, tech_file, ecc_parms, first=True, last=True, display=(verbosity > 1))

    # BL_StartChannel
    api.StartChannel(id_, channel)

    # experiment loop
    csvfile = open("ocv.csv", "w")
    csvfile.write("t (s),Ewe (V)\n")
    count = 0
    print("> Reading data ", end="", flush=True)
    while True:
        # BL_GetData
        data = api.GetData(id_, channel)
        status, tech_name = get_info_data(api, data)
        print(".", end="", flush=True)

        for output in get_experiment_data(api, data, tech_name, board_type):
            csvfile.write(f"{output['t']},{output['Ewe']}\n")
            csvfile.flush()
            count += 1

        if status == "STOP":
            break

        time.sleep(1)

    csvfile.close()
    print()
    print(f"> {count} data have been writted into ocv.csv")
    print("> experiment done")
    newline()

    # BL_Disconnect
    api.Disconnect(id_)

except KeyboardInterrupt:
    print(".. interrupted")

except Exception as e:
    print(exception_brief(e, verbosity >= 1))

# ==============================================================================#
