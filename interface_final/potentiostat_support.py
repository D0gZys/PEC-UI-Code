from __future__ import annotations

import math
import sys
from dataclasses import dataclass
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
KBIO_PARENT = PROJECT_ROOT / "Potentiostat" / "Examples" / "Python"
EC_LAB_DLL_DIR = Path(r"C:\EC-Lab Development Package\lib")
PROJECT_DLL_DIR = PROJECT_ROOT / "Potentiostat" / "lib"

if str(KBIO_PARENT) not in sys.path:
    sys.path.insert(0, str(KBIO_PARENT))


KBIO = None
KBIO_api = None
ECC_parm = None
make_ecc_parm = None
make_ecc_parms = None
get_info_data = None
c_is_64b = None


def import_kbio():
    global KBIO, KBIO_api, ECC_parm, make_ecc_parm, make_ecc_parms
    global get_info_data, c_is_64b

    if KBIO is not None:
        return

    import kbio.kbio_types as _KBIO
    from kbio.c_utils import c_is_64b as _c_is_64b
    from kbio.kbio_api import KBIO_api as _KBIO_api
    from kbio.kbio_tech import ECC_parm as _ECC_parm
    from kbio.kbio_tech import get_info_data as _get_info_data
    from kbio.kbio_tech import make_ecc_parm as _make_ecc_parm
    from kbio.kbio_tech import make_ecc_parms as _make_ecc_parms

    KBIO = _KBIO
    KBIO_api = _KBIO_api
    ECC_parm = _ECC_parm
    make_ecc_parm = _make_ecc_parm
    make_ecc_parms = _make_ecc_parms
    get_info_data = _get_info_data
    c_is_64b = _c_is_64b


CA_PARMS: dict[str, object] = {}


def rebuild_ca_parms():
    import_kbio()
    global CA_PARMS
    CA_PARMS = {
        "voltage_step": ECC_parm("Voltage_step", float),
        "step_duration": ECC_parm("Duration_step", float),
        "vs_init": ECC_parm("vs_initial", bool),
        "nb_steps": ECC_parm("Step_number", int),
        "record_dt": ECC_parm("Record_every_dT", float),
        "record_dI": ECC_parm("Record_every_dI", float),
        "repeat": ECC_parm("N_Cycles", int),
        "I_range": ECC_parm("I_Range", int),
        "E_range": ECC_parm("E_Range", int),
        "bandwidth": ECC_parm("Bandwidth", int),
    }


@dataclass
class VoltageStep:
    voltage: float
    duration: float
    vs_init: bool = False


I_RANGE_LABELS = [
    ("Keep", -1),
    ("100 pA", 0),
    ("1 nA", 1),
    ("10 nA", 2),
    ("100 nA", 3),
    ("1 uA", 4),
    ("10 uA", 5),
    ("100 uA", 6),
    ("1 mA", 7),
    ("10 mA", 8),
    ("100 mA", 9),
    ("1 A", 10),
    ("Booster", 11),
    ("Auto", 12),
]

I_UNIT_LABELS = ["A", "mA", "uA", "nA", "pA"]

E_RANGE_LABELS = [
    ("-2,5 V; 2,5 V", 0),
    ("-5 V; 5 V", 1),
    ("-10 V; 10 V", 2),
    ("Auto", 3),
]

BW_LABELS = [(str(i), i) for i in range(1, 10)]
RECORD_LABELS = ["<I>", "<Ewe>", "<I> and <Ewe>"]
VS_LABELS = ["Ref", "Pref"]


def default_dll_path() -> str:
    candidates = [
        EC_LAB_DLL_DIR,
        PROJECT_DLL_DIR,
    ]
    for base in candidates:
        if base.exists():
            return str(base)
    return ""


def resolve_resource_path(filename: str, dll_dir: str | Path | None = None) -> str:
    if not filename:
        return ""
    path = Path(filename)
    if path.is_file():
        return str(path)
    candidates: list[Path] = []
    if dll_dir:
        candidates.append(Path(dll_dir))
    for base in (EC_LAB_DLL_DIR, PROJECT_DLL_DIR):
        if base not in candidates:
            candidates.append(base)
    for base in candidates:
        candidate = base / filename
        if candidate.is_file():
            return str(candidate)
    if dll_dir:
        return str(Path(dll_dir) / filename)
    return filename


def resolve_label_value(label: str, mapping: list[tuple[str, int]]) -> int:
    for mapped_label, value in mapping:
        if mapped_label == label:
            return value
    raise ValueError(f"Label inconnu : {label}")


def select_ca_tech_file(board_type: int) -> str:
    import_kbio()
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            return "ca.ecc"
        case KBIO.BOARD_TYPE.PREMIUM.value:
            return "ca4.ecc"
        case KBIO.BOARD_TYPE.DIGICORE.value:
            return "ca5.ecc"
        case _:
            raise RuntimeError(f"Type de carte inconnu ({board_type}).")


def select_firmware(board_type: int) -> tuple[str, str]:
    import_kbio()
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            return "kernel.bin", "Vmp_ii_0437_a6.xlx"
        case KBIO.BOARD_TYPE.PREMIUM.value:
            return "kernel4.bin", "vmp_iv_0395_aa.xlx"
        case KBIO.BOARD_TYPE.DIGICORE.value:
            return "kernel.bin", ""
        case _:
            raise RuntimeError(f"Type de carte inconnu ({board_type}).")


def value_to_color(value: float, v_min: float, v_max: float) -> str:
    if v_max == v_min:
        return "#cccccc"
    t = max(0.0, min(1.0, (value - v_min) / (v_max - v_min)))
    if t < 0.5:
        s = t * 2.0
        r = int(60 + s * 195)
        g = int(60 + s * 195)
        b = 255
    else:
        s = (t - 0.5) * 2.0
        r = 255
        g = int(255 - s * 195)
        b = int(255 - s * 195)
    return f"#{r:02x}{g:02x}{b:02x}"


def format_current(value: float) -> str:
    av = abs(value)
    if av == 0:
        return "0"
    if av >= 1:
        return f"{value:.3f} A"
    if av >= 1e-3:
        return f"{value * 1e3:.2f} mA"
    if av >= 1e-6:
        return f"{value * 1e6:.2f} uA"
    if av >= 1e-9:
        return f"{value * 1e9:.2f} nA"
    return f"{value:.2e}"


def normalize_view_bounds(x_min: float, x_max: float, y_min: float, y_max: float) -> tuple[float, float, float, float]:
    if math.isclose(x_max, x_min):
        x_max = x_min + 1.0
    if math.isclose(y_max, y_min):
        delta = abs(y_min) * 0.1 if not math.isclose(y_min, 0.0) else 1.0
        y_min -= delta
        y_max += delta
    return x_min, x_max, y_min, y_max


