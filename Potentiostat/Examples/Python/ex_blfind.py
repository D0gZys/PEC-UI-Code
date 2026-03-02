#####################################################################
# This document is a part of the BioLogic OEM Package and is
# protected by the terms of the OEM Package licence as well as
# other intellectual property rights owned by BioLogic SAS.
# This document may only be used for non-commercial purposes
# such as for the integration of BioLogic equipment to larger
# technical solutions manufactured  and/or delivered to end-users.
#####################################################################
""" Bio-Logic OEM package python API.

Script shown as an example of how to discover Biologic instruments
using the EC-Lab OEM Package library.

The script uses parameters which are provided below.

"""

import os

import kbio.kbio_types as KBIO
from kbio.c_utils import c_is_64b
from kbio.kbio_api import KBIO_api
from kbio.utils import exception_brief

# ------------------------------------------------------------------------------#

# Test parameters, to be adjusted

# the kind of search is one of : {'all'|'ethernet'|'usb'}

kind = "usb"
verbosity = 3

binary_path = os.environ.get("ECLIB_DIR", f"C:{os.sep}EC-Lab Development Package{os.sep}lib")

# ------------------------------------------------------------------------------#

# one liner functions


def newline():
    print()


def print_separator():
    print("#" + "-" * 78 + "#")


def print_exception(e):
    print(f"{exception_brief(e,verbosity)}")


# determine DLL files according to Python version (32b/64b)

if c_is_64b:
    eclib = "EClib64.dll"
    blfind = "blfind64.dll"
else:
    eclib = "EClib.dll"
    blfind = "blfind.dll"

eclib_path = f"{binary_path}{os.sep}{eclib}"
blfind_path = f"{binary_path}{os.sep}{blfind}"

# ==============================================================================#

""" Example main code : use discovery functions with the  package API. """

try:
    # API initialize
    api = KBIO_api(eclib_path, blfind_path)

    # discover instruments

    if kind == "all":
        # BL_FindEChemDev
        instruments = api.FindEChemDev()
    elif kind == "usb":
        # BL_FindEChemUsbDev
        instruments = api.FindEChemUsbDev()
    elif kind == "ethernet":
        # BL_FindEChemEthDev
        instruments = api.FindEChemEthDev()
    else:
        raise RuntimeError(f"type ({kind}) must be one of [all,usb,ethernet]")

    if instruments:
        for instrument in instruments:
            print_separator()

            if isinstance(instrument, KBIO.USB_device):
                # print discovered information
                print(f"{instrument.address} : ", end="")

                index = instrument.index

                # BL_GetUSBdeviceinfos
                usb_info = api.GetUSBDeviceInfos(index)
                print(f"{usb_info}")

                address = instrument.address

            elif isinstance(instrument, KBIO.Ethernet_device):
                # extract information from instrument type
                address = instrument.config[0]
                name = instrument.name.strip()
                serial_nb = instrument.serial.strip()
                dev_id = instrument.identifier.strip()

                # print discovered information
                print(f"device @ {address} : '{name}', s/n '{serial_nb}', id '{dev_id}'")

            else:
                raise RuntimeError(f"unknown device type ({instrument})")

            # Now print brief information about instrument channels ..

            # BL_Connect
            id_, device_info = api.Connect(address)

            version = f"v{device_info.FirmwareVersion/100:.2f}"
            print(f"> {device_info.model}, {version}")

            # BL_GetChannelsPlugged
            # .. GetChannelsPlugged is a generator, expand into a set
            channels = {*api.GetChannelsPlugged(id_)}

            for channel in sorted(channels):
                # BL_GetChannelInfos
                channel_info = api.GetChannelInfo(id_, channel)
                print(f">   channel {channel:2} : {channel_info.board} board, ", end="")
                if channel_info.has_no_firmware:
                    print("no firmware")
                elif channel_info.is_kernel_loaded:
                    version = channel_info.FirmwareVersion / 1000
                    version = f"{version*10:.2f}" if version < 1.0 else f"{version:.3f}"
                    print(f"{channel_info.firmware} (v{version})")
                else:
                    version = channel_info.FirmwareVersion / 100
                    version = f"{version*10:.2f}" if version < 1.0 else f"{version:.3f}"
                    print(f"{channel_info.firmware} (v{version})")

            api.Disconnect(id_)

        print_separator()

    else:
        print("no EC instruments detected")

except KBIO_api.BL_Error as e:
    print(f"discover : {e!r}")

except KeyboardInterrupt:
    print(".. interrupted")

except Exception as e:
    print_exception(e)

# ==============================================================================#
