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
from dataclasses import dataclass

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

# CP parameter values
cp3_tech_file = "cp.ecc"
cp4_tech_file = "cp4.ecc"
cp5_tech_file = "cp5.ecc"

repeat_count = 2
record_dt = 0.1  # seconds
record_dE = 0.1  # Volts
i_range = "I_RANGE_10mA"

# dictionary of CP parameters (non exhaustive)

CP_parms = {
    "current_step": ECC_parm("Current_step", float),
    "step_duration": ECC_parm("Duration_step", float),
    "vs_init": ECC_parm("vs_initial", bool),
    "nb_steps": ECC_parm("Step_number", int),
    "record_dt": ECC_parm("Record_every_dT", float),
    "record_dE": ECC_parm("Record_every_dE", float),
    "repeat": ECC_parm("N_Cycles", int),
    "I_range": ECC_parm("I_Range", int),
}

# defining a current step parameter


@dataclass
class current_step:
    current: float
    duration: float
    vs_init: bool = False


# list of step parameters
steps = [
    current_step(0.001, 2),  # 1mA during 2s
    current_step(0.002, 1),  # 2mA during 1s
    current_step(0.0005, 3, True),  # 0.5mA delta during 3s
]

# ==============================================================================#

# helper functions


def newline():
    print()


def print_exception(e):
    print(f"{exception_brief(e, verbosity>=2)}")


def print_messages(ch):
    """Repeatedly retrieve and print messages for a given channel."""
    while True:
        # BL_GetMessage
        msg = api.GetMessage(id_, ch)
        if not msg:
            break
        print(msg)


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
  * create a CP parameter list (a subset of all possible parameters),
  * load the CP technique into the channel,
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
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            tech_file = cp3_tech_file
        case KBIO.BOARD_TYPE.PREMIUM.value:
            tech_file = cp4_tech_file
        case KBIO.BOARD_TYPE.DIGICORE.value:
            tech_file = cp5_tech_file
        case _:
            print("> Board type detection failed")
            sys.exit(-1)

    # BL_Define<xxx>Parameter
    p_steps = list()

    for idx, step in enumerate(steps):
        parm = make_ecc_parm(api, CP_parms["current_step"], step.current, idx)
        p_steps.append(parm)
        parm = make_ecc_parm(api, CP_parms["step_duration"], step.duration, idx)
        p_steps.append(parm)
        parm = make_ecc_parm(api, CP_parms["vs_init"], step.vs_init, idx)
        p_steps.append(parm)

    # number of steps is one less than len(steps)
    p_nb_steps = make_ecc_parm(api, CP_parms["nb_steps"], idx)

    # record parameters
    p_record_dt = make_ecc_parm(api, CP_parms["record_dt"], record_dt)
    p_record_dE = make_ecc_parm(api, CP_parms["record_dE"], record_dE)

    # repeating factor
    p_repeat = make_ecc_parm(api, CP_parms["repeat"], repeat_count)
    p_I_range = make_ecc_parm(api, CP_parms["I_range"], KBIO.I_RANGE[i_range].value)

    # make the technique parameter array
    ecc_parms = make_ecc_parms(api, *p_steps, p_nb_steps, p_record_dt, p_record_dE, p_I_range, p_repeat)

    # BL_LoadTechnique
    api.LoadTechnique(id_, channel, tech_file, ecc_parms, first=True, last=True, display=(verbosity > 1))

    # BL_StartChannel
    api.StartChannel(id_, channel)

    # experiment loop
    csvfile = open("cp.csv", "w")
    csvfile.write("t (s),Ewe (V),Iwe (A),Cycle (N)\n")
    count = 0
    print("> Reading data ", end="", flush=True)
    while True:
        # BL_GetData
        data = api.GetData(id_, channel)
        status, tech_name = get_info_data(api, data)
        print(".", end="", flush=True)

        for output in get_experiment_data(api, data, tech_name, board_type):
            csvfile.write(f"{output['t']},{output['Ewe']},{output['Iwe']},{output['cycle']}\n")
            csvfile.flush()
            count += 1

        if status == "STOP":
            break

        time.sleep(1)

    csvfile.close()
    print()
    print(f"> {count} data have been writted into cp.csv")
    print("> experiment done")
    newline()

    # BL_Disconnect
    api.Disconnect(id_)

except KeyboardInterrupt:
    print(".. interrupted")

except Exception as e:
    print_exception(e)

# ==============================================================================#
