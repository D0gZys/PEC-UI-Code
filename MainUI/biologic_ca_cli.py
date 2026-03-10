"""
Script CLI — Chronoampérométrie BioLogic SP-300 (sans interface graphique)

Se connecte au potentiostat, charge le firmware, lance une expérience CA
et affiche les valeurs brutes reçues dans le terminal.

Paramètres par défaut identiques à biologic_potentiostat_test_ui.py :
  - IP           : 169.254.3.150
  - Canal        : 1
  - Ewe          : 0.500 V  vs. Ref
  - Durée        : 2 mn 10 s  (130 s)
  - Record dta   : 0.1000 s
  - E Range      : -2.5 V / 2.5 V  (index 0)
  - I Range      : Auto            (index 12)
  - Bandwidth    : 8
  - N Cycles     : 0

Usage :
  python biologic_ca_cli.py
  python biologic_ca_cli.py --ip 192.168.1.5 --channel 2 --voltage 0.200 --duration 60
"""

import argparse
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# kbio path setup
# ---------------------------------------------------------------------------

_PROJECT_ROOT = Path(__file__).resolve().parent.parent
_KBIO_PARENT = _PROJECT_ROOT / "Potentiostat" / "Examples" / "Python"
if str(_KBIO_PARENT) not in sys.path:
    sys.path.insert(0, str(_KBIO_PARENT))

# ---------------------------------------------------------------------------
# Imports kbio
# ---------------------------------------------------------------------------

try:
    import kbio.kbio_types as KBIO
    from kbio.kbio_api import KBIO_api
    from kbio.kbio_tech import (
        ECC_parm,
        get_info_data,
        make_ecc_parm,
        make_ecc_parms,
    )
    from kbio.c_utils import c_is_64b
except ImportError as e:
    print(f"[ERREUR] Impossible d'importer la bibliothèque kbio : {e}")
    print(f"         Vérifier que le dossier suivant existe :")
    print(f"         {_KBIO_PARENT}")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Paramètres CA (noms ECC identiques à EC-Lab)
# ---------------------------------------------------------------------------

CA_PARMS = {
    "voltage_step": ECC_parm("Voltage_step",    float),
    "step_duration": ECC_parm("Duration_step",   float),
    "vs_init":       ECC_parm("vs_initial",      bool),
    "nb_steps":      ECC_parm("Step_number",     int),
    "record_dt":     ECC_parm("Record_every_dT", float),
    "repeat":        ECC_parm("N_Cycles",        int),
    "I_range":       ECC_parm("I_Range",         int),
    "E_range":       ECC_parm("E_Range",         int),
    "bandwidth":     ECC_parm("Bandwidth",       int),
}

# Valeurs par défaut identiques à l'interface graphique
def _default_eclib_dir() -> str:
    candidates = [
        _PROJECT_ROOT / "Potentiostat" / "lib",
        Path(r"C:\EC-Lab Development Package\lib"),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return r"C:\EC-Lab Development Package\lib"


DEFAULTS = {
    "dll_dir":      _default_eclib_dir(),
    "ip":           "169.254.3.150",
    "channel":      1,
    "voltage":      0.500,       # V
    "vs_init":      False,       # False = vs. Ref,  True = vs. Pref
    "duration":     130.0,       # s  (0h 2mn 10s)
    "record_dt":    0.1000,      # s
    "n_cycles":     0,
    "i_range":      12,          # Auto
    "e_range":      0,           # -2.5 V / 2.5 V
    "bandwidth":    8,
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str):
    t = time.strftime("%H:%M:%S")
    print(f"[{t}] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(
        description="Chronoampérométrie CLI — BioLogic SP-300"
    )
    p.add_argument("--dll",      default=DEFAULTS["dll_dir"],  help="Dossier contenant EClib64.dll")
    p.add_argument("--ip",       default=DEFAULTS["ip"],       help="Adresse IP du potentiostat")
    p.add_argument("--channel",  default=DEFAULTS["channel"],  type=int, help="Canal (1-16)")
    p.add_argument("--voltage",  default=DEFAULTS["voltage"],  type=float, help="Tension Ewe (V)")
    p.add_argument("--vs-pref",  action="store_true",          help="vs. Pref (vs. Ref par défaut)")
    p.add_argument("--duration", default=DEFAULTS["duration"], type=float, help="Durée du pas (s)")
    p.add_argument("--dt",       default=DEFAULTS["record_dt"],type=float, help="Intervalle enregistrement (s)")
    p.add_argument("--cycles",   default=DEFAULTS["n_cycles"], type=int,   help="Nombre de cycles (0=illimité)")
    p.add_argument("--i-range",  default=DEFAULTS["i_range"],  type=int,   help="I Range index (12=Auto)")
    p.add_argument("--e-range",  default=DEFAULTS["e_range"],  type=int,   help="E Range index (0=-2.5/2.5V)")
    p.add_argument("--bandwidth",default=DEFAULTS["bandwidth"],type=int,   help="Bandwidth (1-9)")
    return p.parse_args()


def select_tech_file(board_type: int) -> str:
    """Retourne le nom du fichier .ecc selon le type de carte."""
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            return "ca.ecc"
        case KBIO.BOARD_TYPE.PREMIUM.value:
            return "ca4.ecc"
        case KBIO.BOARD_TYPE.DIGICORE.value:
            return "ca5.ecc"
        case _:
            raise RuntimeError(f"Type de carte non reconnu : {board_type}")


def select_firmware(board_type: int) -> tuple[str, str]:
    """Retourne (firmware, fpga) selon le type de carte."""
    match board_type:
        case KBIO.BOARD_TYPE.ESSENTIAL.value:
            return "kernel.bin", "Vmp_ii_0437_a6.xlx"
        case KBIO.BOARD_TYPE.PREMIUM.value:
            return "kernel4.bin", "vmp_iv_0395_aa.xlx"
        case KBIO.BOARD_TYPE.DIGICORE.value:
            return "kernel.bin", ""
        case _:
            raise RuntimeError(f"Type de carte non reconnu : {board_type}")


def parse_data_records(api: KBIO_api, data, board_type: int) -> list[dict]:
    """Extrait les enregistrements bruts d'une réponse GetData."""
    current_values, data_info, data_record = data
    records = []
    ix = 0

    for _ in range(data_info.NbRows):
        inx = ix + data_info.NbCols
        if inx > len(data_record):
            break

        raw_row = data_record[ix:inx]
        t_high, t_low, *cols = raw_row

        t_rel = (t_high << 32) + t_low
        t = current_values.TimeBase * t_rel

        rec = {"t": t, "raw": list(raw_row)}

        if len(cols) >= 2:
            Ewe = api.ConvertChannelNumericIntoSingle(cols[0], board_type)
            I   = api.ConvertChannelNumericIntoSingle(cols[1], board_type)
            cycle = cols[2] if len(cols) >= 3 else 0
            rec.update({"Ewe": Ewe, "I": I, "cycle": cycle})
        elif len(cols) == 1:
            Ewe = api.ConvertChannelNumericIntoSingle(cols[0], board_type)
            rec.update({"Ewe": Ewe, "I": float("nan"), "cycle": 0})

        records.append(rec)
        ix = inx

    return records


# ---------------------------------------------------------------------------
# Programme principal
# ---------------------------------------------------------------------------

def main():
    args = parse_args()

    # --- Sélection de la DLL ---
    dll_name = "EClib64.dll" if c_is_64b else "EClib.dll"
    dll_path = Path(args.dll) / dll_name
    if not dll_path.is_file():
        log(f"ERREUR  DLL introuvable : {dll_path}")
        log("        Installer le EC-Lab Development Package ou spécifier --dll")
        sys.exit(1)

    log(f"DLL     : {dll_path}")
    log(f"IP      : {args.ip}  |  Canal : {args.channel}")
    log(f"Tension : {args.voltage:.3f} V  vs. {'Pref' if args.vs_pref else 'Ref'}")
    log(f"Durée   : {args.duration} s  |  dta : {args.dt} s  |  Cycles : {args.cycles}")
    log(f"I Range : {args.i_range}  |  E Range : {args.e_range}  |  BW : {args.bandwidth}")
    print("-" * 70, flush=True)

    # --- Chargement de la DLL ---
    api = KBIO_api(str(dll_path))
    version = api.GetLibVersion()
    log(f"EClib version : {version}")

    # --- Connexion ---
    log(f"Connexion à {args.ip}…")
    connection_id, device_info = api.Connect(args.ip, timeout=10)
    model = KBIO.DEVICE(device_info.DeviceCode).name
    log(f"Connecté : {model}  |  {device_info.NumberOfChannels} canaux")

    ch = args.channel
    board_type = api.GetChannelBoardType(connection_id, ch)
    log(f"Canal {ch}  |  board_type = {board_type}")

    # --- Chargement du firmware ---
    fw, fpga = select_firmware(board_type)
    log(f"Chargement firmware : {fw}  (FPGA : {fpga or 'aucun'})")
    ch_map = api.channel_map({ch})
    api.LoadFirmware(connection_id, ch_map, firmware=fw, fpga=fpga, force=True)
    log("Firmware chargé.")

    # --- Construction des paramètres ECC ---
    tech_file = select_tech_file(board_type)
    log(f"Technique : {tech_file}")

    ecc_params = make_ecc_parms(
        api,
        make_ecc_parm(api, CA_PARMS["voltage_step"],  args.voltage,   0),
        make_ecc_parm(api, CA_PARMS["step_duration"],  args.duration,  0),
        make_ecc_parm(api, CA_PARMS["vs_init"],         args.vs_pref,  0),
        make_ecc_parm(api, CA_PARMS["nb_steps"],        0),
        make_ecc_parm(api, CA_PARMS["record_dt"],       args.dt),
        make_ecc_parm(api, CA_PARMS["repeat"],          args.cycles),
        make_ecc_parm(api, CA_PARMS["I_range"],         args.i_range),
        make_ecc_parm(api, CA_PARMS["E_range"],         args.e_range),
        make_ecc_parm(api, CA_PARMS["bandwidth"],       args.bandwidth),
    )

    # --- Chargement de la technique ---
    api.LoadTechnique(connection_id, ch, tech_file, ecc_params,
                      first=True, last=True, display=False)
    log("Technique CA chargée.")

    # --- Démarrage ---
    api.StartChannel(connection_id, ch)
    log(f"Canal {ch} démarré. Acquisition… (Ctrl+C pour arrêter)")
    print("-" * 70, flush=True)

    # En-tête colonne
    print(f"{'t (s)':>14}  {'Ewe (V)':>14}  {'I (A)':>14}  {'cycle':>6}  {'raw':}", flush=True)
    print("-" * 70, flush=True)

    total_points = 0

    try:
        while True:
            try:
                data = api.GetData(connection_id, ch)
                status, tech_name = get_info_data(api, data)

                records = parse_data_records(api, data, board_type)

                for rec in records:
                    total_points += 1
                    t       = rec.get("t",     float("nan"))
                    Ewe     = rec.get("Ewe",   float("nan"))
                    I       = rec.get("I",     float("nan"))
                    cycle   = rec.get("cycle", 0)
                    raw     = rec.get("raw",   [])
                    print(
                        f"{t:>14.6f}  {Ewe:>14.6e}  {I:>14.6e}  {cycle:>6}  "
                        f"raw={[hex(v) if isinstance(v, int) else v for v in raw]}",
                        flush=True,
                    )

                if status == "STOP":
                    print("-" * 70, flush=True)
                    log(f"Expérience terminée — {total_points} points acquis.")
                    break

            except Exception as poll_exc:
                log(f"Erreur lecture : {poll_exc}")

            time.sleep(0.5)

    except KeyboardInterrupt:
        print(flush=True)
        log("Interruption utilisateur.")

    finally:
        log("Arrêt du canal…")
        try:
            api.StopChannel(connection_id, ch)
        except Exception:
            pass
        log("Déconnexion…")
        try:
            api.Disconnect(connection_id)
        except Exception:
            pass
        log(f"Fin — {total_points} points enregistrés.")


if __name__ == "__main__":
    main()
