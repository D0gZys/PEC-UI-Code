#!/usr/bin/env python3
"""
Potentiostat subprocess bridge for LaserBench C++.
Communicates via stdin/stdout with JSON messages (one per line).
"""

import sys
import json
import os

# Locate kbio library relative to this script
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_KBIO_CANDIDATES = [
    os.path.join(_SCRIPT_DIR, "..", "..", "Potentiostat", "Examples", "Python"),
    os.path.join(_SCRIPT_DIR, "..", "Potentiostat", "Examples", "Python"),
]
for _p in _KBIO_CANDIDATES:
    if os.path.isdir(os.path.join(_p, "kbio")):
        sys.path.insert(0, _p)
        break

try:
    from kbio.kbio_api import KBIO_api
    import kbio.kbio_types as KBIO
    from kbio.c_utils import c_is_64b
    from kbio.kbio_tech import make_ecc_parm, make_ecc_parms, ECC_parm, get_experiment_data, get_info_data
    _KBIO_OK = True
    _KBIO_ERR = ""
except ImportError as _e:
    _KBIO_OK = False
    _KBIO_ERR = str(_e)

if _KBIO_OK:
    CA_PARMS = {
        "voltage_step":  ECC_parm("Voltage_step",    float),
        "step_duration": ECC_parm("Duration_step",   float),
        "vs_init":       ECC_parm("vs_initial",      bool),
        "nb_steps":      ECC_parm("Step_number",     int),
        "record_dt":     ECC_parm("Record_every_dT", float),
        "I_range":       ECC_parm("I_Range",         int),
        "E_range":       ECC_parm("E_Range",         int),
        "bandwidth":     ECC_parm("Bandwidth",       int),
        "n_cycles":      ECC_parm("N_Cycles",        int),
    }
    OCV_PARMS = {
        "duration":  ECC_parm("Rest_time_T",     float),
        "record_dt": ECC_parm("Record_every_dT", float),
        "E_range":   ECC_parm("E_Range",         int),
    }

_api = None
_connection_id = None
_board_type = None
_dll_dir = None


def _send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def _handle_connect(cmd):
    global _api, _connection_id, _board_type, _dll_dir
    dll_path = cmd.get("dll_path", "")
    address  = cmd.get("address", "169.254.3.150")
    channel  = cmd.get("channel", 1)

    if not _KBIO_OK:
        _send({"ok": False, "error": f"kbio non disponible: {_KBIO_ERR}"})
        return

    # Auto-detect DLL if path empty or not found
    if not dll_path or not os.path.isfile(dll_path):
        dll_name = "EClib64.dll" if c_is_64b else "EClib.dll"
        candidates = [
            os.path.join(os.environ.get("ECLIB_DIR", ""), dll_name),
            os.path.join("C:\\EC-Lab Development Package\\lib", dll_name),
        ]
        for c in candidates:
            if os.path.isfile(c):
                dll_path = c
                break
        if not dll_path or not os.path.isfile(dll_path):
            _send({"ok": False, "error": f"DLL EClib introuvable. Specifier le chemin dans l'interface."})
            return

    try:
        _api = KBIO_api(dll_path)
        _dll_dir = os.path.dirname(dll_path)
        _connection_id, info = _api.Connect(address, timeout=10)
        _board_type = _api.GetChannelBoardType(_connection_id, channel)
        model = KBIO.DEVICE(info.DeviceCode).name
        _send({"ok": True, "model": model, "board_type": int(_board_type)})
    except Exception as e:
        _api = None; _connection_id = None; _board_type = None; _dll_dir = None
        _send({"ok": False, "error": str(e)})


def _handle_disconnect(cmd):
    global _api, _connection_id, _board_type, _dll_dir
    if _api is not None and _connection_id is not None:
        try:
            _api.Disconnect(_connection_id)
        except Exception:
            pass
    _api = None; _connection_id = None; _board_type = None; _dll_dir = None
    _send({"ok": True})


def _handle_load_firmware(cmd):
    channel = cmd.get("channel", 1)
    if _api is None or _connection_id is None:
        _send({"ok": False, "error": "Non connecte"}); return
    try:
        bt = _board_type
        if bt == KBIO.BOARD_TYPE.ESSENTIAL.value:
            fw, fpga = "kernel.bin", "Vmp_ii_0437_a6.xlx"
        elif bt == KBIO.BOARD_TYPE.PREMIUM.value:
            fw, fpga = "kernel4.bin", "vmp_iv_0395_aa.xlx"
        elif bt == KBIO.BOARD_TYPE.DIGICORE.value:
            fw, fpga = "kernel.bin", ""
        else:
            _send({"ok": False, "error": f"Type de carte inconnu: {bt}"}); return
        ch_map = _api.channel_map({channel})
        _api.LoadFirmware(_connection_id, ch_map, firmware=fw, fpga=fpga, force=True)
        _send({"ok": True})
    except Exception as e:
        _send({"ok": False, "error": str(e)})


def _handle_start_ca(cmd):
    channel   = cmd.get("channel",   1)
    voltage   = cmd.get("voltage",   0.5)
    duration  = cmd.get("duration",  1.0)
    vs_init   = bool(cmd.get("vs_init", False))
    record_dt = cmd.get("record_dt", 0.1)
    i_range   = cmd.get("i_range",  12)
    e_range   = cmd.get("e_range",   0)
    bandwidth = cmd.get("bandwidth", 8)
    n_cycles  = cmd.get("n_cycles",  0)
    if _api is None or _connection_id is None:
        _send({"ok": False, "error": "Non connecte"}); return
    bt = _board_type
    if bt == KBIO.BOARD_TYPE.ESSENTIAL.value: tech_name = "ca.ecc"
    elif bt == KBIO.BOARD_TYPE.PREMIUM.value:  tech_name = "ca4.ecc"
    elif bt == KBIO.BOARD_TYPE.DIGICORE.value: tech_name = "ca5.ecc"
    else: _send({"ok": False, "error": f"Type de carte inconnu: {bt}"}); return
    tech = tech_name
    if _dll_dir:
        full = os.path.join(_dll_dir, tech_name)
        if os.path.isfile(full):
            tech = full
    try:
        parms = [
            make_ecc_parm(_api, CA_PARMS["voltage_step"],  voltage,   0),
            make_ecc_parm(_api, CA_PARMS["step_duration"], duration,  0),
            make_ecc_parm(_api, CA_PARMS["vs_init"],       vs_init,   0),
            make_ecc_parm(_api, CA_PARMS["nb_steps"],      0),
            make_ecc_parm(_api, CA_PARMS["record_dt"],     record_dt),
            make_ecc_parm(_api, CA_PARMS["I_range"],       i_range),
            make_ecc_parm(_api, CA_PARMS["E_range"],       e_range),
            make_ecc_parm(_api, CA_PARMS["bandwidth"],     bandwidth),
            make_ecc_parm(_api, CA_PARMS["n_cycles"],      n_cycles),
        ]
        ecc_parms = make_ecc_parms(_api, *parms)
        _api.LoadTechnique(_connection_id, channel, tech, ecc_parms, first=True, last=True)
        _api.StartChannel(_connection_id, channel)
        _send({"ok": True})
    except Exception as e:
        _send({"ok": False, "error": str(e)})


def _handle_start_ocv(cmd):
    channel   = cmd.get("channel",   1)
    duration  = cmd.get("duration",  1.0)
    record_dt = cmd.get("record_dt", 0.1)
    e_range   = cmd.get("e_range",   0)
    if _api is None or _connection_id is None:
        _send({"ok": False, "error": "Non connecte"}); return
    bt = _board_type
    if bt == KBIO.BOARD_TYPE.ESSENTIAL.value: tech = "ocv.ecc"
    elif bt == KBIO.BOARD_TYPE.PREMIUM.value:  tech = "ocv4.ecc"
    elif bt == KBIO.BOARD_TYPE.DIGICORE.value: tech = "ocv5.ecc"
    else: _send({"ok": False, "error": f"Type de carte inconnu: {bt}"}); return
    try:
        parms = [
            make_ecc_parm(_api, OCV_PARMS["duration"],  duration),
            make_ecc_parm(_api, OCV_PARMS["record_dt"], record_dt),
            make_ecc_parm(_api, OCV_PARMS["E_range"],   e_range),
        ]
        ecc_parms = make_ecc_parms(_api, *parms)
        _api.LoadTechnique(_connection_id, channel, tech, ecc_parms, first=True, last=True)
        _api.StartChannel(_connection_id, channel)
        _send({"ok": True})
    except Exception as e:
        _send({"ok": False, "error": str(e)})


def _handle_stop_channel(cmd):
    channel = cmd.get("channel", 1)
    if _api is None or _connection_id is None:
        _send({"ok": False, "error": "Non connecte"}); return
    try:
        _api.StopChannel(_connection_id, channel)
        _send({"ok": True})
    except Exception as e:
        _send({"ok": False, "error": str(e)})


def _handle_get_data(cmd):
    channel = cmd.get("channel", 1)
    if _api is None or _connection_id is None:
        _send({"ok": False, "error": "Non connecte"}); return
    try:
        data = _api.GetData(_connection_id, channel)
        status, tech_name = get_info_data(_api, data)
        rows = []
        for output in get_experiment_data(_api, data, tech_name, _board_type):
            rows.append({"t": output.get("t", 0.0), "Ewe": output.get("Ewe", 0.0), "I": output.get("I", 0.0)})
        _send({"ok": True, "status": status, "data": rows})
    except Exception as e:
        _send({"ok": False, "error": str(e)})


def _handle_get_current_values(cmd):
    channel = cmd.get("channel", 1)
    if _api is None or _connection_id is None:
        _send({"ok": False, "error": "Non connecte"}); return
    try:
        cv = _api.GetCurrentValues(_connection_id, channel)
        state = getattr(cv, "State", 0)
        stopped = (state == 0)
        elapsed_time = float(getattr(cv, "ElapsedTime", 0.0))
        ewe = float(getattr(cv, "Ewe", 0.0))
        I = float(getattr(cv, "I", 0.0))
        _send({"ok": True, "stopped": stopped, "ElapsedTime": elapsed_time, "Ewe": ewe, "I": I})
    except Exception as e:
        _send({"ok": False, "error": str(e)})


_HANDLERS = {
    "connect":            _handle_connect,
    "disconnect":         _handle_disconnect,
    "load_firmware":      _handle_load_firmware,
    "start_ca":           _handle_start_ca,
    "start_ocv":          _handle_start_ocv,
    "stop_channel":       _handle_stop_channel,
    "get_data":           _handle_get_data,
    "get_current_values": _handle_get_current_values,
}


def main():
    _send({"ready": True, "kbio_available": _KBIO_OK, "error": _KBIO_ERR if not _KBIO_OK else ""})
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            cmd = json.loads(line)
        except json.JSONDecodeError as e:
            _send({"ok": False, "error": f"JSON invalide: {e}"}); continue
        action = cmd.get("cmd", "")
        if action == "quit":
            break
        handler = _HANDLERS.get(action)
        if handler is None:
            _send({"ok": False, "error": f"Commande inconnue: {action}"})
        else:
            handler(cmd)


if __name__ == "__main__":
    main()
