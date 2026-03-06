"""
Interface de test pour le potentiostat BioLogic SP-300.

Permet de :
- Se connecter à l'instrument via Ethernet
- Charger le firmware sur un canal
- Configurer et lancer une expérience de Chronoampérométrie (CA)
- Visualiser en temps réel les courbes I(t) et E(t)
- Exporter les données en CSV
- Mode Cartographie : remplir une matrice NxM en échantillonnant I à intervalle régulier

Utilise la bibliothèque kbio fournie dans le Developer Package BioLogic.
"""

import csv
import math
import os
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import filedialog, messagebox

import tkinter as tk
from tkinter import ttk

# ---------------------------------------------------------------------------
# Path setup: make the kbio package importable
# ---------------------------------------------------------------------------

_PROJECT_ROOT = Path(__file__).resolve().parent.parent
_KBIO_PARENT = _PROJECT_ROOT / "Potentiostat" / "Examples" / "Python"
if str(_KBIO_PARENT) not in sys.path:
    sys.path.insert(0, str(_KBIO_PARENT))

# ---------------------------------------------------------------------------
# Lazy imports from kbio (so the UI can start even if DLL is absent)
# ---------------------------------------------------------------------------

KBIO = None
KBIO_api = None
ECC_parm = None
make_ecc_parm = None
make_ecc_parms = None
get_info_data = None
get_experiment_data = None
c_is_64b = None


def _import_kbio():
    """Import kbio modules on first use."""
    global KBIO, KBIO_api, ECC_parm, make_ecc_parm, make_ecc_parms
    global get_info_data, get_experiment_data, c_is_64b
    import kbio.kbio_types as _KBIO
    from kbio.kbio_api import KBIO_api as _KBIO_api
    from kbio.kbio_tech import ECC_parm as _ECC_parm
    from kbio.kbio_tech import make_ecc_parm as _make_ecc_parm
    from kbio.kbio_tech import make_ecc_parms as _make_ecc_parms
    from kbio.kbio_tech import get_info_data as _get_info_data
    from kbio.kbio_tech import get_experiment_data as _get_experiment_data
    from kbio.c_utils import c_is_64b as _c_is_64b

    KBIO = _KBIO
    KBIO_api = _KBIO_api
    ECC_parm = _ECC_parm
    make_ecc_parm = _make_ecc_parm
    make_ecc_parms = _make_ecc_parms
    get_info_data = _get_info_data
    get_experiment_data = _get_experiment_data
    c_is_64b = _c_is_64b


# ---------------------------------------------------------------------------
# CA technique parameter definitions (matches EC-Lab parameter names)
# ---------------------------------------------------------------------------

CA_PARMS = {
    "voltage_step": ECC_parm("Voltage_step", float) if ECC_parm else None,
    "step_duration": ECC_parm("Duration_step", float) if ECC_parm else None,
    "vs_init": ECC_parm("vs_initial", bool) if ECC_parm else None,
    "nb_steps": ECC_parm("Step_number", int) if ECC_parm else None,
    "record_dt": ECC_parm("Record_every_dT", float) if ECC_parm else None,
    "record_dI": ECC_parm("Record_every_dI", float) if ECC_parm else None,
    "repeat": ECC_parm("N_Cycles", int) if ECC_parm else None,
    "I_range": ECC_parm("I_Range", int) if ECC_parm else None,
    "E_range": ECC_parm("E_Range", int) if ECC_parm else None,
    "bandwidth": ECC_parm("Bandwidth", int) if ECC_parm else None,
}


def _rebuild_ca_parms():
    """Rebuild CA_PARMS dict after kbio is imported."""
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


# ---------------------------------------------------------------------------
# Voltage step dataclass
# ---------------------------------------------------------------------------

@dataclass
class VoltageStep:
    voltage: float  # Volts
    duration: float  # seconds
    vs_init: bool = False  # relative to initial voltage


# ---------------------------------------------------------------------------
# I_RANGE / E_RANGE / BANDWIDTH user-friendly labels
# ---------------------------------------------------------------------------

I_RANGE_LABELS = [
    ("Keep", -1),
    ("100 pA", 0),
    ("1 nA", 1),
    ("10 nA", 2),
    ("100 nA", 3),
    ("1 µA", 4),
    ("10 µA", 5),
    ("100 µA", 6),
    ("1 mA", 7),
    ("10 mA", 8),
    ("100 mA", 9),
    ("1 A", 10),
    ("Booster", 11),
    ("Auto", 12),
]

I_UNIT_LABELS = ["A", "mA", "µA", "nA", "pA"]

E_RANGE_LABELS = [
    ("-2,5 V; 2,5 V", 0),
    ("-5 V; 5 V", 1),
    ("-10 V; 10 V", 2),
    ("Auto", 3),
]

BW_LABELS = [
    ("1", 1),
    ("2", 2),
    ("3", 3),
    ("4", 4),
    ("5", 5),
    ("6", 6),
    ("7", 7),
    ("8", 8),
    ("9", 9),
]

RECORD_LABELS = ["<I>", "<Ewe>", "<I> and <Ewe>"]

VS_LABELS = ["Ref", "Pref"]


# ---------------------------------------------------------------------------
# Heatmap color helpers
# ---------------------------------------------------------------------------

def _value_to_color(value: float, v_min: float, v_max: float) -> str:
    """Map a value to a blue-white-red gradient color."""
    if v_max == v_min:
        return "#cccccc"
    t = max(0.0, min(1.0, (value - v_min) / (v_max - v_min)))
    # Blue (0) -> White (0.5) -> Red (1)
    if t < 0.5:
        s = t * 2  # 0..1
        r = int(60 + s * 195)
        g = int(60 + s * 195)
        b = 255
    else:
        s = (t - 0.5) * 2  # 0..1
        r = 255
        g = int(255 - s * 195)
        b = int(255 - s * 195)
    return f"#{r:02x}{g:02x}{b:02x}"


def _format_current(value: float) -> str:
    """Format a current value for display in matrix cells."""
    av = abs(value)
    if av == 0:
        return "0"
    if av >= 1:
        return f"{value:.3f} A"
    if av >= 1e-3:
        return f"{value * 1e3:.2f} mA"
    if av >= 1e-6:
        return f"{value * 1e6:.2f} µA"
    if av >= 1e-9:
        return f"{value * 1e9:.2f} nA"
    return f"{value:.2e}"


# ===========================================================================
# Main Application
# ===========================================================================


class BiologicPotentiostatTestApp(tk.Tk):
    """Interface de test pour piloter un BioLogic SP-300 — Chronoampérométrie (CA)."""

    def __init__(self):
        super().__init__()
        self.title("BioLogic SP-300 — Chronoampérométrie (CA)")
        self.geometry("1200x850")
        self.minsize(900, 650)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        # ---- State ----
        self.api = None
        self.connection_id = None
        self.board_type = None
        self.device_info = None
        self._experiment_thread = None
        self._stop_event = threading.Event()
        self._data_rows: list[dict] = []

        # ---- Tk variables ----
        self.dll_path_var = tk.StringVar(value=self._default_dll_path())
        self.address_var = tk.StringVar(value="169.254.3.150")
        self.channel_var = tk.IntVar(value=1)
        self.status_var = tk.StringVar(value="Déconnecté")

        # CA parameters — matching EC-Lab naming
        self.record_mode_var = tk.StringVar(value="<I>")
        self.record_dta_var = tk.StringVar(value="0.1000")
        self.e_range_var = tk.StringVar(value="-2,5 V; 2,5 V")
        self.i_range_var = tk.StringVar(value="Auto")
        self.bw_var = tk.StringVar(value="8")
        self.n_cycles_var = tk.StringVar(value="0")

        # Graph data
        self._plot_t: list[float] = []
        self._plot_I: list[float] = []
        self._plot_E: list[float] = []

        # Graph type selection
        self.graph_type_var = tk.StringVar(value="I = f(t)")

        # Zoom / pan state
        self._view_x_min = None
        self._view_x_max = None
        self._view_y_min = None
        self._view_y_max = None
        self._pan_start = None

        # ---- Cartography state ----
        self.carto_rows_var = tk.IntVar(value=3)
        self.carto_cols_var = tk.IntVar(value=3)
        self.carto_interval_var = tk.StringVar(value="5.0")
        self._matrix_data: list[list[float | None]] = []
        self._matrix_index = 0  # linear index in serpentine order
        self._carto_running = False

        self._build_ui()
        self._log("Application prête. Configurer le chemin DLL et l'adresse IP de l'instrument.")

    # -----------------------------------------------------------------------
    # Default paths
    # -----------------------------------------------------------------------

    @staticmethod
    def _default_dll_path() -> str:
        base = Path(r"C:\EC-Lab Development Package\lib")
        if base.exists():
            return str(base)
        return ""

    # -----------------------------------------------------------------------
    # UI construction
    # -----------------------------------------------------------------------

    def _build_ui(self):
        main = ttk.Frame(self, padding=6)
        main.pack(fill="both", expand=True)

        # ---- Connection frame (always visible) ----
        self._build_connection_frame(main)

        # ---- Notebook (2 tabs) ----
        self.notebook = ttk.Notebook(main)
        self.notebook.pack(fill="both", expand=True, pady=(4, 0))

        # Tab 1: Configuration
        config_tab = ttk.Frame(self.notebook)
        self.notebook.add(config_tab, text="  Configuration  ")
        self._build_config_tab(config_tab)

        # Tab 2: Mesure (Graphique + Cartographie ensemble)
        mesure_tab = ttk.Frame(self.notebook)
        self.notebook.add(mesure_tab, text="  Mesure  ")
        self._build_mesure_tab(mesure_tab)

        # ---- Log ----
        log_frame = ttk.LabelFrame(main, text="Journal", padding=4)
        log_frame.pack(fill="x", pady=(4, 0))
        self.log_text = tk.Text(log_frame, height=5, width=100, state="normal", font=("Consolas", 9))
        scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.log_text.pack(fill="both", expand=True)

    # ---- Connection frame ----

    def _build_connection_frame(self, parent):
        conn_frame = ttk.LabelFrame(parent, text="Connexion Instrument", padding=8)
        conn_frame.pack(fill="x", pady=(0, 4))

        ttk.Label(conn_frame, text="Chemin DLL :").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn_frame, textvariable=self.dll_path_var, width=70).grid(row=0, column=1, columnspan=4, sticky="ew", padx=4)
        ttk.Button(conn_frame, text="Parcourir…", command=self._browse_dll).grid(row=0, column=5, padx=4)

        ttk.Label(conn_frame, text="Adresse IP :").grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Entry(conn_frame, textvariable=self.address_var, width=20).grid(row=1, column=1, sticky="w", padx=4, pady=(6, 0))

        ttk.Label(conn_frame, text="Canal :").grid(row=1, column=2, sticky="e", pady=(6, 0))
        ch_spin = ttk.Spinbox(conn_frame, from_=1, to=16, textvariable=self.channel_var, width=4)
        ch_spin.grid(row=1, column=3, sticky="w", padx=4, pady=(6, 0))

        btn_frame = ttk.Frame(conn_frame)
        btn_frame.grid(row=1, column=4, columnspan=2, pady=(6, 0), sticky="e")
        ttk.Button(btn_frame, text="Connecter", command=self._on_connect).pack(side="left", padx=2)
        ttk.Button(btn_frame, text="Charger Firmware", command=self._on_load_firmware).pack(side="left", padx=2)
        ttk.Button(btn_frame, text="Déconnecter", command=self._on_disconnect).pack(side="left", padx=2)

        ttk.Label(conn_frame, textvariable=self.status_var, foreground="blue", font=("", 9, "bold")).grid(
            row=2, column=0, columnspan=6, sticky="w", pady=(4, 0)
        )
        conn_frame.columnconfigure(1, weight=1)

    # ---- Tab 1: Configuration ----

    def _build_config_tab(self, parent):
        # Scrollable frame for all parameters
        canvas = tk.Canvas(parent, highlightthickness=0)
        scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        scrollable_frame = ttk.Frame(canvas, padding=8)

        scrollable_frame.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        scrollbar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)

        # Section 1: Voltage Steps
        steps_lf = ttk.LabelFrame(scrollable_frame, text="Apply Ewe (Voltage Steps)", padding=6)
        steps_lf.pack(fill="x", pady=(0, 6))

        ttk.Label(steps_lf, text="#", width=3, anchor="center").grid(row=0, column=0)
        ttk.Label(steps_lf, text="Ewe (V)", width=9, anchor="center").grid(row=0, column=1)
        ttk.Label(steps_lf, text="vs.", width=5, anchor="center").grid(row=0, column=2)
        ttk.Label(steps_lf, text="h", width=4, anchor="center").grid(row=0, column=3)
        ttk.Label(steps_lf, text="mn", width=4, anchor="center").grid(row=0, column=4)
        ttk.Label(steps_lf, text="s", width=8, anchor="center").grid(row=0, column=5)
        ttk.Label(steps_lf, text="", width=3).grid(row=0, column=6)

        self._steps_frame = steps_lf
        self._step_widgets: list[dict] = []
        self._step_row_offset = 1

        self._add_step_row(voltage=0.500, hours=0, minutes=2, seconds=10.0, vs="Ref")

        btn_row = ttk.Frame(steps_lf)
        btn_row.grid(row=100, column=0, columnspan=7, pady=(4, 0))
        ttk.Button(btn_row, text="+ Ajouter pas", command=lambda: self._add_step_row()).pack(side="left", padx=2)

        # Section 2: Limits
        limits_lf = ttk.LabelFrame(scrollable_frame, text="Limits", padding=6)
        limits_lf.pack(fill="x", pady=(0, 6))

        ttk.Label(limits_lf, text="Imax =").grid(row=0, column=0, sticky="e")
        self.imax_var = tk.StringVar(value="pass")
        ttk.Entry(limits_lf, textvariable=self.imax_var, width=10).grid(row=0, column=1, padx=4, sticky="w")
        self.imax_unit_var = tk.StringVar(value="mA")
        ttk.Combobox(limits_lf, textvariable=self.imax_unit_var, values=I_UNIT_LABELS, state="readonly", width=5).grid(row=0, column=2, sticky="w")

        ttk.Label(limits_lf, text="Imin =").grid(row=1, column=0, sticky="e", pady=(4, 0))
        self.imin_var = tk.StringVar(value="pass")
        ttk.Entry(limits_lf, textvariable=self.imin_var, width=10).grid(row=1, column=1, padx=4, sticky="w", pady=(4, 0))
        self.imin_unit_var = tk.StringVar(value="mA")
        ttk.Combobox(limits_lf, textvariable=self.imin_unit_var, values=I_UNIT_LABELS, state="readonly", width=5).grid(row=1, column=2, sticky="w", pady=(4, 0))

        ttk.Label(limits_lf, text="|ΔQ| > ΔQM =").grid(row=2, column=0, sticky="e", pady=(4, 0))
        self.dqm_var = tk.StringVar(value="0.000")
        ttk.Entry(limits_lf, textvariable=self.dqm_var, width=10).grid(row=2, column=1, padx=4, sticky="w", pady=(4, 0))
        self.dqm_unit_var = tk.StringVar(value="mA.h")
        ttk.Combobox(limits_lf, textvariable=self.dqm_unit_var, values=["A.h", "mA.h", "µA.h"], state="readonly", width=5).grid(row=2, column=2, sticky="w", pady=(4, 0))

        # Section 3: Record
        rec_lf = ttk.LabelFrame(scrollable_frame, text="Record", padding=6)
        rec_lf.pack(fill="x", pady=(0, 6))

        ttk.Label(rec_lf, text="Record").grid(row=0, column=0, sticky="e")
        ttk.Combobox(rec_lf, textvariable=self.record_mode_var, values=RECORD_LABELS, state="readonly", width=14).grid(row=0, column=1, padx=4, sticky="w")

        ttk.Label(rec_lf, text="every dta =").grid(row=1, column=0, sticky="e", pady=(4, 0))
        rec_frame = ttk.Frame(rec_lf)
        rec_frame.grid(row=1, column=1, sticky="w", padx=4, pady=(4, 0))
        ttk.Entry(rec_frame, textvariable=self.record_dta_var, width=10).pack(side="left")
        ttk.Label(rec_frame, text="s").pack(side="left", padx=(2, 0))

        # Section 4: Ranges
        range_lf = ttk.LabelFrame(scrollable_frame, text="Ranges", padding=6)
        range_lf.pack(fill="x", pady=(0, 6))

        ttk.Label(range_lf, text="E Range =").grid(row=0, column=0, sticky="e")
        ttk.Combobox(range_lf, textvariable=self.e_range_var, values=[l for l, _ in E_RANGE_LABELS], state="readonly", width=16).grid(row=0, column=1, padx=4, sticky="w")

        ttk.Label(range_lf, text="I Range =").grid(row=1, column=0, sticky="e", pady=(4, 0))
        ttk.Combobox(range_lf, textvariable=self.i_range_var, values=[l for l, _ in I_RANGE_LABELS], state="readonly", width=16).grid(row=1, column=1, padx=4, sticky="w", pady=(4, 0))

        ttk.Label(range_lf, text="Bandwidth =").grid(row=2, column=0, sticky="e", pady=(4, 0))
        ttk.Combobox(range_lf, textvariable=self.bw_var, values=[l for l, _ in BW_LABELS], state="readonly", width=16).grid(row=2, column=1, padx=4, sticky="w", pady=(4, 0))

        # Section 5: N Cycles
        cyc_lf = ttk.LabelFrame(scrollable_frame, text="Cycles", padding=6)
        cyc_lf.pack(fill="x", pady=(0, 6))
        ttk.Label(cyc_lf, text="N Cycles (0 = illimité) :").grid(row=0, column=0, sticky="w")
        ttk.Entry(cyc_lf, textvariable=self.n_cycles_var, width=8).grid(row=0, column=1, padx=4, sticky="w")

        # Section 6: Experiment control
        ctrl_lf = ttk.LabelFrame(scrollable_frame, text="Contrôle expérience CA", padding=6)
        ctrl_lf.pack(fill="x", pady=(0, 6))

        self.btn_start = ttk.Button(ctrl_lf, text="▶  Démarrer CA", command=self._on_start_experiment)
        self.btn_start.pack(fill="x", pady=2)
        self.btn_stop = ttk.Button(ctrl_lf, text="■  Arrêter", command=self._on_stop_experiment, state="disabled")
        self.btn_stop.pack(fill="x", pady=2)
        self.btn_export = ttk.Button(ctrl_lf, text="Exporter CSV", command=self._on_export_csv, state="disabled")
        self.btn_export.pack(fill="x", pady=2)

        self.progress_var = tk.StringVar(value="")
        ttk.Label(ctrl_lf, textvariable=self.progress_var, foreground="green").pack(fill="x", pady=(4, 0))

    # ---- Tab 2: Mesure (Graph on top, Cartography on bottom) ----

    def _build_mesure_tab(self, parent):
        # ── Top bar: cartography config + controls ──
        config_frame = ttk.LabelFrame(parent, text="Cartographie", padding=6)
        config_frame.pack(fill="x", pady=(0, 2))

        ttk.Label(config_frame, text="Lignes :").grid(row=0, column=0, sticky="w", padx=(0, 4))
        ttk.Spinbox(config_frame, from_=1, to=50, textvariable=self.carto_rows_var, width=5).grid(row=0, column=1, padx=(0, 12))

        ttk.Label(config_frame, text="Colonnes :").grid(row=0, column=2, sticky="w", padx=(0, 4))
        ttk.Spinbox(config_frame, from_=1, to=50, textvariable=self.carto_cols_var, width=5).grid(row=0, column=3, padx=(0, 12))

        ttk.Label(config_frame, text="Intervalle (s) :").grid(row=0, column=4, sticky="w", padx=(0, 4))
        ttk.Entry(config_frame, textvariable=self.carto_interval_var, width=8).grid(row=0, column=5, padx=(0, 12))

        self.btn_carto_start = ttk.Button(config_frame, text="▶  Démarrer cartographie", command=self._on_start_cartography)
        self.btn_carto_start.grid(row=0, column=6, padx=4)
        self.btn_carto_stop = ttk.Button(config_frame, text="■  Arrêter", command=self._on_stop_experiment, state="disabled")
        self.btn_carto_stop.grid(row=0, column=7, padx=4)

        self.btn_carto_export = ttk.Button(config_frame, text="Exporter CSV", command=self._on_export_matrix_csv, state="disabled")
        self.btn_carto_export.grid(row=0, column=8, padx=4)

        self.carto_progress_var = tk.StringVar(value="")
        ttk.Label(config_frame, textvariable=self.carto_progress_var, foreground="green", font=("", 9, "bold")).grid(
            row=0, column=9, sticky="w", padx=(8, 0)
        )

        # ── PanedWindow: graph (top) + matrix (bottom) ──
        paned = ttk.PanedWindow(parent, orient="vertical")
        paned.pack(fill="both", expand=True, pady=(2, 0))

        # ── Top pane: Graph ──
        graph_frame = ttk.Frame(paned)
        paned.add(graph_frame, weight=1)

        graph_top = ttk.Frame(graph_frame)
        graph_top.pack(fill="x")
        ttk.Label(graph_top, text="Affichage :").pack(side="left", padx=(4, 4))
        for gt in ["I = f(t)", "Ewe = f(t)", "I = f(Ewe)", "Ewe = f(I)"]:
            ttk.Radiobutton(graph_top, text=gt, value=gt, variable=self.graph_type_var,
                            command=self._update_plot).pack(side="left", padx=3)
        ttk.Button(graph_top, text="Plein écran", command=self._on_fullscreen_graph).pack(side="right", padx=2)
        ttk.Button(graph_top, text="Reset vue", command=self._reset_view).pack(side="right", padx=2)
        self.data_count_var = tk.StringVar(value="Points : 0")
        ttk.Label(graph_top, textvariable=self.data_count_var).pack(side="right", padx=(8, 4))

        self.graph_canvas = tk.Canvas(graph_frame, bg="white", highlightthickness=0, height=200)
        self.graph_canvas.pack(fill="both", expand=True)
        self.graph_canvas.bind("<Configure>", lambda e: self._update_plot())
        self.graph_canvas.bind("<MouseWheel>", self._on_graph_scroll)
        self.graph_canvas.bind("<Button-4>", self._on_graph_scroll)
        self.graph_canvas.bind("<Button-5>", self._on_graph_scroll)
        self.graph_canvas.bind("<ButtonPress-1>", self._on_graph_pan_start)
        self.graph_canvas.bind("<B1-Motion>", self._on_graph_pan_move)
        self.graph_canvas.bind("<ButtonRelease-1>", self._on_graph_pan_end)

        # ── Bottom pane: Matrix heatmap ──
        matrix_frame = ttk.Frame(paned)
        paned.add(matrix_frame, weight=2)

        self.matrix_canvas = tk.Canvas(matrix_frame, bg="#f0f0f0", highlightthickness=0)
        self.matrix_canvas.pack(side="left", fill="both", expand=True)

        self.legend_canvas = tk.Canvas(matrix_frame, bg="#f0f0f0", width=80, highlightthickness=0)
        self.legend_canvas.pack(side="right", fill="y", padx=(4, 0))

        self.matrix_canvas.bind("<Configure>", lambda e: self._update_matrix_display())

    # -----------------------------------------------------------------------
    # Voltage step management
    # -----------------------------------------------------------------------

    def _add_step_row(self, voltage: float = 0.0, hours: int = 0, minutes: int = 0,
                       seconds: float = 5.0, vs: str = "Ref"):
        idx = len(self._step_widgets)
        row = self._step_row_offset + idx

        v_var = tk.StringVar(value=f"{voltage:.3f}")
        vs_var = tk.StringVar(value=vs)
        h_var = tk.StringVar(value=str(hours))
        m_var = tk.StringVar(value=str(minutes))
        s_var = tk.StringVar(value=f"{seconds:.4f}")

        lbl = ttk.Label(self._steps_frame, text=str(idx + 1), width=3, anchor="center")
        lbl.grid(row=row, column=0)
        e_v = ttk.Entry(self._steps_frame, textvariable=v_var, width=9)
        e_v.grid(row=row, column=1, padx=2)
        cb_vs = ttk.Combobox(self._steps_frame, textvariable=vs_var, values=VS_LABELS, state="readonly", width=4)
        cb_vs.grid(row=row, column=2, padx=1)
        e_h = ttk.Entry(self._steps_frame, textvariable=h_var, width=4)
        e_h.grid(row=row, column=3, padx=1)
        e_m = ttk.Entry(self._steps_frame, textvariable=m_var, width=4)
        e_m.grid(row=row, column=4, padx=1)
        e_s = ttk.Entry(self._steps_frame, textvariable=s_var, width=8)
        e_s.grid(row=row, column=5, padx=1)
        btn_del = ttk.Button(self._steps_frame, text="✕", width=3, command=lambda i=idx: self._remove_step_row(i))
        btn_del.grid(row=row, column=6)

        self._step_widgets.append({
            "lbl": lbl, "e_v": e_v, "cb_vs": cb_vs, "e_h": e_h, "e_m": e_m, "e_s": e_s, "btn": btn_del,
            "v_var": v_var, "vs_var": vs_var, "h_var": h_var, "m_var": m_var, "s_var": s_var,
        })

    def _remove_step_row(self, idx: int):
        if len(self._step_widgets) <= 1:
            return
        w = self._step_widgets.pop(idx)
        for widget_key in ("lbl", "e_v", "cb_vs", "e_h", "e_m", "e_s", "btn"):
            w[widget_key].destroy()
        for i, sw in enumerate(self._step_widgets):
            sw["lbl"].configure(text=str(i + 1))

    def _get_steps(self) -> list[VoltageStep]:
        steps = []
        for sw in self._step_widgets:
            v = float(sw["v_var"].get())
            h = int(sw["h_var"].get())
            m = int(sw["m_var"].get())
            s = float(sw["s_var"].get())
            duration = h * 3600 + m * 60 + s
            vs_init = sw["vs_var"].get() == "Pref"
            steps.append(VoltageStep(voltage=v, duration=duration, vs_init=vs_init))
        return steps

    # -----------------------------------------------------------------------
    # Logging
    # -----------------------------------------------------------------------

    def _log(self, msg: str):
        t = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{t}] {msg}\n")
        self.log_text.see("end")

    def _log_safe(self, msg: str):
        """Thread-safe log."""
        self.after(0, self._log, msg)

    def _set_status(self, msg: str):
        self.status_var.set(msg)

    # -----------------------------------------------------------------------
    # Connection
    # -----------------------------------------------------------------------

    def _browse_dll(self):
        d = filedialog.askdirectory(title="Sélectionner le dossier contenant EClib64.dll")
        if d:
            self.dll_path_var.set(d)

    def _ensure_api(self):
        """Create the KBIO_api instance if not already done."""
        if self.api is not None:
            return
        _import_kbio()
        _rebuild_ca_parms()

        dll_dir = self.dll_path_var.get().strip()
        if not dll_dir:
            raise RuntimeError("Le chemin DLL n'est pas renseigné.")

        dll_name = "EClib64.dll" if c_is_64b else "EClib.dll"
        dll_path = os.path.join(dll_dir, dll_name)
        if not os.path.isfile(dll_path):
            raise FileNotFoundError(f"DLL introuvable : {dll_path}")

        self.api = KBIO_api(dll_path)
        version = self.api.GetLibVersion()
        self._log(f"EClib chargée — version {version}")

    def _on_connect(self):
        try:
            self._ensure_api()
            address = self.address_var.get().strip()
            if not address:
                raise ValueError("Adresse IP vide.")
            self._log(f"Connexion à {address}…")
            self.connection_id, self.device_info = self.api.Connect(address, timeout=10)
            model = KBIO.DEVICE(self.device_info.DeviceCode).name
            ch = self.channel_var.get()
            self.board_type = self.api.GetChannelBoardType(self.connection_id, ch)
            self._set_status(f"Connecté — {model} ({address})")
            self._log(f"Connecté : {model}, {self.device_info.NumberOfChannels} canaux, board_type={self.board_type}")
        except Exception as exc:
            messagebox.showerror("Erreur de connexion", str(exc))
            self._set_status("Erreur de connexion")
            self._log(f"Erreur : {exc}")

    def _on_load_firmware(self):
        try:
            if self.connection_id is None:
                raise RuntimeError("Non connecté.")
            ch = self.channel_var.get()
            bt = self.board_type

            match bt:
                case KBIO.BOARD_TYPE.ESSENTIAL.value:
                    fw, fpga = "kernel.bin", "Vmp_ii_0437_a6.xlx"
                case KBIO.BOARD_TYPE.PREMIUM.value:
                    fw, fpga = "kernel4.bin", "vmp_iv_0395_aa.xlx"
                case KBIO.BOARD_TYPE.DIGICORE.value:
                    fw, fpga = "kernel.bin", ""
                case _:
                    raise RuntimeError(f"Type de carte inconnu ({bt}).")

            self._log(f"Chargement firmware {fw} sur canal {ch}…")
            ch_map = self.api.channel_map({ch})
            self.api.LoadFirmware(self.connection_id, ch_map, firmware=fw, fpga=fpga, force=True)
            self._log("Firmware chargé avec succès.")

            ch_info = self.api.GetChannelInfo(self.connection_id, ch)
            self._log(f"Canal {ch} — kernel loaded: {ch_info.is_kernel_loaded if hasattr(ch_info, 'is_kernel_loaded') else 'N/A'}")
            self._set_status("Firmware chargé — Prêt")
        except Exception as exc:
            messagebox.showerror("Erreur firmware", str(exc))
            self._log(f"Erreur firmware : {exc}")

    def _on_disconnect(self):
        self._on_stop_experiment()
        if self.connection_id is not None:
            try:
                self.api.Disconnect(self.connection_id)
            except Exception:
                pass
            self.connection_id = None
            self.board_type = None
        self._set_status("Déconnecté")
        self._log("Déconnecté de l'instrument.")

    # -----------------------------------------------------------------------
    # Shared: resolve label, build ECC params, select tech file
    # -----------------------------------------------------------------------

    def _resolve_label_value(self, label: str, mapping: list[tuple[str, int]]) -> int:
        for lbl, val in mapping:
            if lbl == label:
                return val
        raise ValueError(f"Label inconnu : {label}")

    def _select_tech_file(self) -> str:
        bt = self.board_type
        match bt:
            case KBIO.BOARD_TYPE.ESSENTIAL.value:
                return "ca.ecc"
            case KBIO.BOARD_TYPE.PREMIUM.value:
                return "ca4.ecc"
            case KBIO.BOARD_TYPE.DIGICORE.value:
                return "ca5.ecc"
            case _:
                raise RuntimeError(f"Type de carte inconnu ({bt}).")

    def _build_ecc_params(self, steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val):
        """Build the ECC parameter block for a CA technique."""
        p_steps_list = []
        for idx, step in enumerate(steps):
            p_steps_list.append(make_ecc_parm(self.api, CA_PARMS["voltage_step"], step.voltage, idx))
            p_steps_list.append(make_ecc_parm(self.api, CA_PARMS["step_duration"], step.duration, idx))
            p_steps_list.append(make_ecc_parm(self.api, CA_PARMS["vs_init"], step.vs_init, idx))

        p_nb_steps = make_ecc_parm(self.api, CA_PARMS["nb_steps"], len(steps) - 1)
        p_record_dt = make_ecc_parm(self.api, CA_PARMS["record_dt"], record_dt)
        p_repeat = make_ecc_parm(self.api, CA_PARMS["repeat"], n_cycles)
        p_i_range = make_ecc_parm(self.api, CA_PARMS["I_range"], i_range_val)
        p_e_range = make_ecc_parm(self.api, CA_PARMS["E_range"], e_range_val)
        p_bw = make_ecc_parm(self.api, CA_PARMS["bandwidth"], bw_val)

        return make_ecc_parms(
            self.api,
            *p_steps_list,
            p_nb_steps,
            p_record_dt,
            p_i_range,
            p_e_range,
            p_bw,
            p_repeat,
        )

    def _read_ca_params(self):
        """Read CA parameters from the UI and return (steps, record_dt, n_cycles, i_range, e_range, bw)."""
        steps = self._get_steps()
        if not steps:
            raise ValueError("Au moins un pas de tension est requis.")
        record_dt = float(self.record_dta_var.get())
        n_cycles = int(self.n_cycles_var.get())
        i_range_val = self._resolve_label_value(self.i_range_var.get(), I_RANGE_LABELS)
        e_range_val = self._resolve_label_value(self.e_range_var.get(), E_RANGE_LABELS)
        bw_val = self._resolve_label_value(self.bw_var.get(), BW_LABELS)
        return steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val

    def _load_and_start_ca(self, steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val):
        """Load a CA technique and start the channel. Returns the channel number."""
        ch = self.channel_var.get()
        tech_file = self._select_tech_file()
        self._log_safe(f"Technique : {tech_file}")

        ecc_parms = self._build_ecc_params(steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val)

        self.api.LoadTechnique(self.connection_id, ch, tech_file, ecc_parms, first=True, last=True, display=False)
        self._log_safe("Technique CA chargée.")

        self.api.StartChannel(self.connection_id, ch)
        self._log_safe(f"Canal {ch} démarré — acquisition en cours…")
        return ch

    def _parse_ca_data(self, data, tech_name):
        """Parse CA experiment data records."""
        current_values, data_info, data_record = data
        ix = 0
        bt = self.board_type

        for _ in range(data_info.NbRows):
            inx = ix + data_info.NbCols
            if inx > len(data_record):
                break

            t_high, t_low, *row = data_record[ix:inx]
            t_rel = (t_high << 32) + t_low
            t = current_values.TimeBase * t_rel

            if len(row) >= 2:
                Ewe = self.api.ConvertChannelNumericIntoSingle(row[0], bt)
                I = self.api.ConvertChannelNumericIntoSingle(row[1], bt)
                cycle = row[2] if len(row) >= 3 else 0
                yield {"t": t, "Ewe": Ewe, "I": I, "cycle": cycle}
            elif len(row) == 1:
                Ewe = self.api.ConvertChannelNumericIntoSingle(row[0], bt)
                yield {"t": t, "Ewe": Ewe, "I": 0.0, "cycle": 0}

            ix = inx

    # -----------------------------------------------------------------------
    # Experiment (standard CA)
    # -----------------------------------------------------------------------

    def _on_start_experiment(self):
        if self.connection_id is None:
            messagebox.showwarning("Non connecté", "Connectez-vous d'abord à l'instrument.")
            return
        if self._experiment_thread is not None and self._experiment_thread.is_alive():
            messagebox.showinfo("En cours", "Une expérience est déjà en cours.")
            return

        try:
            params = self._read_ca_params()
        except Exception as exc:
            messagebox.showerror("Paramètres invalides", str(exc))
            return

        # Clear previous data
        self._data_rows.clear()
        self._plot_t.clear()
        self._plot_I.clear()
        self._plot_E.clear()
        self._clear_canvas(self.graph_canvas)
        self.data_count_var.set("Points : 0")

        self._stop_event.clear()
        self._carto_running = False
        self.btn_start.configure(state="disabled")
        self.btn_stop.configure(state="normal")
        self.btn_export.configure(state="disabled")

        # Switch to Graph tab
        self.notebook.select(1)

        self._experiment_thread = threading.Thread(
            target=self._run_experiment,
            args=params,
            daemon=True,
        )
        self._experiment_thread.start()

    def _run_experiment(self, steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val):
        """Background thread: load CA technique, start, poll data until STOP."""
        try:
            ch = self._load_and_start_ca(steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val)

            while not self._stop_event.is_set():
                try:
                    data = self.api.GetData(self.connection_id, ch)
                    status, tech_name = get_info_data(self.api, data)

                    for record in self._parse_ca_data(data, tech_name):
                        self._data_rows.append(record)
                        self._plot_t.append(record["t"])
                        self._plot_I.append(record["I"])
                        self._plot_E.append(record["Ewe"])

                    self.after(0, self._update_plot)

                    if status == "STOP":
                        self._log_safe(f"Expérience terminée — {len(self._data_rows)} points acquis.")
                        break

                except Exception as poll_exc:
                    self._log_safe(f"Erreur lecture données : {poll_exc}")

                time.sleep(0.5)

        except Exception as exc:
            self._log_safe(f"Erreur expérience : {exc}")
        finally:
            self.after(0, self._experiment_finished)

    def _experiment_finished(self):
        self.btn_start.configure(state="normal")
        self.btn_stop.configure(state="disabled")
        self.btn_carto_start.configure(state="normal")
        self.btn_carto_stop.configure(state="disabled")
        if self._data_rows:
            self.btn_export.configure(state="normal")
        if self._carto_running:
            self._carto_running = False
            if any(v is not None for row in self._matrix_data for v in row):
                self.btn_carto_export.configure(state="normal")
            self._update_matrix_display()
        self.progress_var.set(f"Terminé — {len(self._data_rows)} points")

    def _on_stop_experiment(self):
        self._stop_event.set()
        if self.connection_id is not None:
            try:
                ch = self.channel_var.get()
                self.api.StopChannel(self.connection_id, ch)
                self._log("Canal arrêté.")
            except Exception:
                pass

    # -----------------------------------------------------------------------
    # Cartography
    # -----------------------------------------------------------------------

    def _serpentine_order(self, rows: int, cols: int) -> list[tuple[int, int]]:
        """Generate (row, col) indices in serpentine (boustrophedon) order."""
        order = []
        for r in range(rows):
            if r % 2 == 0:
                for c in range(cols):
                    order.append((r, c))
            else:
                for c in range(cols - 1, -1, -1):
                    order.append((r, c))
        return order

    def _on_start_cartography(self):
        if self.connection_id is None:
            messagebox.showwarning("Non connecté", "Connectez-vous d'abord à l'instrument.")
            return
        if self._experiment_thread is not None and self._experiment_thread.is_alive():
            messagebox.showinfo("En cours", "Une expérience/cartographie est déjà en cours.")
            return

        try:
            params = self._read_ca_params()
            rows = self.carto_rows_var.get()
            cols = self.carto_cols_var.get()
            interval = float(self.carto_interval_var.get())
            if rows < 1 or cols < 1:
                raise ValueError("Lignes et colonnes doivent être >= 1.")
            if interval <= 0:
                raise ValueError("L'intervalle doit être > 0.")
        except Exception as exc:
            messagebox.showerror("Paramètres invalides", str(exc))
            return

        # Init matrix
        self._matrix_data = [[None] * cols for _ in range(rows)]
        self._matrix_index = 0
        self._carto_running = True

        # Clear standard data too
        self._data_rows.clear()
        self._plot_t.clear()
        self._plot_I.clear()
        self._plot_E.clear()

        self._stop_event.clear()
        self.btn_carto_start.configure(state="disabled")
        self.btn_carto_stop.configure(state="normal")
        self.btn_carto_export.configure(state="disabled")
        self.btn_start.configure(state="disabled")
        self.btn_stop.configure(state="normal")
        total = rows * cols
        self.carto_progress_var.set(f"Démarrage… 0 / {total}")

        # Switch to Cartography tab
        self.notebook.select(1)

        # Draw empty matrix
        self.after(10, self._update_matrix_display)

        self._experiment_thread = threading.Thread(
            target=self._run_cartography,
            args=(*params, rows, cols, interval),
            daemon=True,
        )
        self._experiment_thread.start()

    def _run_cartography(self, steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val,
                          rows, cols, interval):
        """Background thread: run CA and sample I at regular intervals to fill the matrix."""
        total = rows * cols
        order = self._serpentine_order(rows, cols)

        try:
            ch = self._load_and_start_ca(steps, record_dt, n_cycles, i_range_val, e_range_val, bw_val)

            last_I = None
            cell_idx = 0
            next_sample_time = time.monotonic() + interval

            while not self._stop_event.is_set() and cell_idx < total:
                # Poll data to keep buffer fresh and get latest I
                try:
                    data = self.api.GetData(self.connection_id, ch)
                    status, tech_name = get_info_data(self.api, data)

                    for record in self._parse_ca_data(data, tech_name):
                        self._data_rows.append(record)
                        self._plot_t.append(record["t"])
                        self._plot_I.append(record["I"])
                        self._plot_E.append(record["Ewe"])
                        last_I = record["I"]

                    # Update graph in real time
                    self.after(0, self._update_plot)

                    if status == "STOP":
                        self._log_safe("Le potentiostat a terminé l'expérience avant la fin de la cartographie.")
                        break

                except Exception as poll_exc:
                    self._log_safe(f"Erreur lecture données : {poll_exc}")

                # Check if it's time to sample
                now = time.monotonic()
                if now >= next_sample_time and last_I is not None:
                    r, c = order[cell_idx]
                    self._matrix_data[r][c] = last_I
                    cell_idx += 1
                    self._matrix_index = cell_idx

                    # Update UI
                    self.after(0, self._update_matrix_display)
                    if cell_idx < total:
                        nr, nc = order[cell_idx] if cell_idx < total else (r, c)
                        self.after(0, lambda ci=cell_idx, tot=total, rr=nr, cc=nc:
                                   self.carto_progress_var.set(
                                       f"Case {ci} / {tot} — ligne {rr + 1}, col {cc + 1}"))
                    else:
                        self.after(0, lambda tot=total:
                                   self.carto_progress_var.set(f"Terminé — {tot} / {tot} cases remplies"))

                    next_sample_time = now + interval

                time.sleep(0.3)

            # Stop the channel when matrix is full
            if cell_idx >= total:
                try:
                    self.api.StopChannel(self.connection_id, ch)
                    self._log_safe(f"Cartographie terminée — {total} cases remplies.")
                except Exception:
                    pass

        except Exception as exc:
            self._log_safe(f"Erreur cartographie : {exc}")
        finally:
            self.after(0, self._experiment_finished)

    # -----------------------------------------------------------------------
    # Matrix display (heatmap)
    # -----------------------------------------------------------------------

    def _update_matrix_display(self):
        """Redraw the matrix heatmap on the cartography canvas."""
        canvas = self.matrix_canvas
        canvas.delete("all")

        rows = len(self._matrix_data)
        cols = len(self._matrix_data[0]) if rows > 0 else 0
        if rows == 0 or cols == 0:
            # If no cartography configured yet, show placeholder
            r = self.carto_rows_var.get()
            c = self.carto_cols_var.get()
            if r > 0 and c > 0:
                rows, cols = r, c
                self._matrix_data = [[None] * cols for _ in range(rows)]
            else:
                return

        cw = canvas.winfo_width()
        ch = canvas.winfo_height()
        if cw < 50 or ch < 50:
            return

        margin_left = 50
        margin_top = 30
        margin_right = 10
        margin_bottom = 30

        plot_w = cw - margin_left - margin_right
        plot_h = ch - margin_top - margin_bottom
        if plot_w <= 0 or plot_h <= 0:
            return

        cell_w = plot_w / cols
        cell_h = plot_h / rows

        # Compute value range for color scale
        values = [v for row in self._matrix_data for v in row if v is not None]
        if values:
            v_min, v_max = min(values), max(values)
            if v_min == v_max:
                v_max = v_min + abs(v_min) * 0.1 if v_min != 0 else 1.0
        else:
            v_min, v_max = 0, 1

        # Title
        canvas.create_text(cw // 2, 12, text="Cartographie I (A)", font=("", 11, "bold"), fill="#333")

        # Current cell (for highlight)
        order = self._serpentine_order(rows, cols)
        current_cell = order[self._matrix_index] if self._matrix_index < len(order) else None

        # Draw cells
        for r in range(rows):
            for c in range(cols):
                x0 = margin_left + c * cell_w
                y0 = margin_top + r * cell_h
                x1 = x0 + cell_w
                y1 = y0 + cell_h

                val = self._matrix_data[r][c]

                if val is not None:
                    color = _value_to_color(val, v_min, v_max)
                    canvas.create_rectangle(x0, y0, x1, y1, fill=color, outline="#999")
                    # Text color: dark on light, light on dark
                    t = (val - v_min) / (v_max - v_min) if v_max != v_min else 0.5
                    txt_color = "#000" if 0.2 < t < 0.8 else "#fff"
                    # Adapt font size to cell
                    font_size = max(7, min(11, int(cell_w / 8)))
                    canvas.create_text((x0 + x1) / 2, (y0 + y1) / 2,
                                       text=_format_current(val),
                                       font=("", font_size), fill=txt_color)
                else:
                    canvas.create_rectangle(x0, y0, x1, y1, fill="#e0e0e0", outline="#bbb")

                # Highlight current cell
                if current_cell == (r, c) and self._carto_running:
                    canvas.create_rectangle(x0 + 1, y0 + 1, x1 - 1, y1 - 1,
                                            outline="#ff6600", width=3)

        # Column labels
        for c in range(cols):
            cx = margin_left + (c + 0.5) * cell_w
            canvas.create_text(cx, margin_top + rows * cell_h + 14,
                               text=str(c + 1), font=("", 9), fill="#555")

        # Row labels
        for r in range(rows):
            cy = margin_top + (r + 0.5) * cell_h
            canvas.create_text(margin_left - 10, cy,
                               text=str(r + 1), font=("", 9), fill="#555", anchor="e")

        # Update legend
        self._draw_legend(v_min, v_max, values)

        # Update count
        filled = sum(1 for v in values)
        total = rows * cols
        self.data_count_var.set(f"Points : {len(self._data_rows)}")

    def _draw_legend(self, v_min: float, v_max: float, values: list[float]):
        """Draw color legend on the legend canvas."""
        canvas = self.legend_canvas
        canvas.delete("all")

        cw = canvas.winfo_width()
        ch = canvas.winfo_height()
        if ch < 60 or cw < 30:
            return

        margin_top = 40
        margin_bottom = 30
        bar_x = 10
        bar_w = 25
        bar_h = ch - margin_top - margin_bottom
        if bar_h <= 0:
            return

        canvas.create_text(cw // 2, 12, text="I (A)", font=("", 9, "bold"), fill="#333")

        if not values:
            canvas.create_rectangle(bar_x, margin_top, bar_x + bar_w, margin_top + bar_h,
                                    fill="#e0e0e0", outline="#999")
            return

        # Draw gradient bar (top = max, bottom = min)
        n_steps = min(bar_h, 100)
        step_h = bar_h / n_steps
        for i in range(n_steps):
            frac = 1.0 - i / n_steps  # top = 1.0 (max), bottom = 0.0 (min)
            color = _value_to_color(v_min + frac * (v_max - v_min), v_min, v_max)
            y0 = margin_top + i * step_h
            y1 = y0 + step_h + 1
            canvas.create_rectangle(bar_x, y0, bar_x + bar_w, y1, fill=color, outline="")

        canvas.create_rectangle(bar_x, margin_top, bar_x + bar_w, margin_top + bar_h, outline="#999")

        # Tick labels
        for frac, label_y in [(1.0, margin_top), (0.5, margin_top + bar_h / 2), (0.0, margin_top + bar_h)]:
            val = v_min + frac * (v_max - v_min)
            canvas.create_text(bar_x + bar_w + 5, label_y,
                               text=_format_current(val), font=("", 7), anchor="w", fill="#333")

    # -----------------------------------------------------------------------
    # Matrix CSV export
    # -----------------------------------------------------------------------

    def _on_export_matrix_csv(self):
        if not self._matrix_data or not any(v is not None for row in self._matrix_data for v in row):
            messagebox.showinfo("Aucune donnée", "Pas de données de cartographie à exporter.")
            return

        rows = len(self._matrix_data)
        cols = len(self._matrix_data[0]) if rows > 0 else 0

        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("Tous", "*.*")],
            initialfile="cartography_data.csv",
        )
        if not path:
            return

        try:
            with open(path, "w", newline="") as f:
                writer = csv.writer(f)
                # Header
                writer.writerow([""] + [f"Col {c + 1}" for c in range(cols)])
                # Data
                for r in range(rows):
                    row_data = [f"Ligne {r + 1}"]
                    for c in range(cols):
                        val = self._matrix_data[r][c]
                        row_data.append(f"{val:.6e}" if val is not None else "")
                    writer.writerow(row_data)
            self._log(f"Cartographie exportée : {path} ({rows}x{cols})")
        except Exception as exc:
            messagebox.showerror("Erreur export", str(exc))

    # -----------------------------------------------------------------------
    # Plotting (lightweight canvas-based) — Standard CA
    # -----------------------------------------------------------------------

    def _clear_canvas(self, canvas: tk.Canvas):
        canvas.delete("all")

    def _get_current_data(self):
        gt = self.graph_type_var.get()
        if gt == "I = f(t)":
            return self._plot_t, self._plot_I, "t (s)", "I (A)", "#1f77b4"
        elif gt == "Ewe = f(t)":
            return self._plot_t, self._plot_E, "t (s)", "Ewe (V)", "#d62728"
        elif gt == "I = f(Ewe)":
            return self._plot_E, self._plot_I, "Ewe (V)", "I (A)", "#2ca02c"
        elif gt == "Ewe = f(I)":
            return self._plot_I, self._plot_E, "I (A)", "Ewe (V)", "#9467bd"
        return [], [], "", "", "#000"

    def _data_bounds(self, xs, ys):
        if len(xs) < 1:
            return 0, 1, 0, 1
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        if x_max == x_min:
            x_max = x_min + 1
        if y_max == y_min:
            delta = abs(y_min) * 0.1 if y_min != 0 else 1
            y_min -= delta
            y_max += delta
        return x_min, x_max, y_min, y_max

    def _get_view(self, xs, ys):
        if self._view_x_min is not None:
            return self._view_x_min, self._view_x_max, self._view_y_min, self._view_y_max
        return self._data_bounds(xs, ys)

    def _reset_view(self):
        self._view_x_min = self._view_x_max = None
        self._view_y_min = self._view_y_max = None
        self._update_plot()

    def _canvas_margins(self):
        return 80, 20, 25, 45

    def _pixel_to_data(self, canvas, px, py, x_min, x_max, y_min, y_max):
        ml, mr, mt, mb = self._canvas_margins()
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        pw = w - ml - mr
        ph = h - mt - mb
        if pw <= 0 or ph <= 0:
            return x_min, y_min
        dx = (px - ml) / pw * (x_max - x_min) + x_min
        dy = (1 - (py - mt) / ph) * (y_max - y_min) + y_min
        return dx, dy

    def _on_graph_scroll(self, event):
        xs, ys, *_ = self._get_current_data()
        if len(xs) < 2:
            return
        vx0, vx1, vy0, vy1 = self._get_view(xs, ys)

        if hasattr(event, 'delta'):
            factor = 0.8 if event.delta > 0 else 1.25
        elif event.num == 4:
            factor = 0.8
        else:
            factor = 1.25

        mx, my = self._pixel_to_data(self.graph_canvas, event.x, event.y, vx0, vx1, vy0, vy1)
        self._view_x_min = mx - (mx - vx0) * factor
        self._view_x_max = mx + (vx1 - mx) * factor
        self._view_y_min = my - (my - vy0) * factor
        self._view_y_max = my + (vy1 - my) * factor
        self._update_plot()

    def _on_graph_pan_start(self, event):
        xs, ys, *_ = self._get_current_data()
        if len(xs) < 2:
            return
        vx0, vx1, vy0, vy1 = self._get_view(xs, ys)
        self._pan_start = (event.x, event.y, vx0, vx1, vy0, vy1)

    def _on_graph_pan_move(self, event):
        if self._pan_start is None:
            return
        sx, sy, vx0, vx1, vy0, vy1 = self._pan_start
        ml, mr, mt, mb = self._canvas_margins()
        w = self.graph_canvas.winfo_width()
        h = self.graph_canvas.winfo_height()
        pw = w - ml - mr
        ph = h - mt - mb
        if pw <= 0 or ph <= 0:
            return

        dx_data = -(event.x - sx) / pw * (vx1 - vx0)
        dy_data = (event.y - sy) / ph * (vy1 - vy0)

        self._view_x_min = vx0 + dx_data
        self._view_x_max = vx1 + dx_data
        self._view_y_min = vy0 + dy_data
        self._view_y_max = vy1 + dy_data
        self._update_plot()

    def _on_graph_pan_end(self, _event):
        self._pan_start = None

    # -----------------------------------------------------------------------
    # Fullscreen graph window
    # -----------------------------------------------------------------------

    def _on_fullscreen_graph(self):
        xs, ys, xlabel, ylabel, color = self._get_current_data()
        if len(xs) < 2:
            messagebox.showinfo("Pas de données", "Pas assez de données pour afficher le graphe.")
            return

        win = tk.Toplevel(self)
        win.title(f"{ylabel} = f({xlabel})")
        win.state("zoomed")

        top_bar = ttk.Frame(win, padding=4)
        top_bar.pack(fill="x")
        fs_graph_var = tk.StringVar(value=self.graph_type_var.get())

        fs_state = {"vx0": None, "vx1": None, "vy0": None, "vy1": None, "pan": None}

        def fs_get_data():
            gt = fs_graph_var.get()
            if gt == "I = f(t)":
                return self._plot_t, self._plot_I, "t (s)", "I (A)", "#1f77b4"
            elif gt == "Ewe = f(t)":
                return self._plot_t, self._plot_E, "t (s)", "Ewe (V)", "#d62728"
            elif gt == "I = f(Ewe)":
                return self._plot_E, self._plot_I, "Ewe (V)", "I (A)", "#2ca02c"
            elif gt == "Ewe = f(I)":
                return self._plot_I, self._plot_E, "I (A)", "Ewe (V)", "#9467bd"
            return [], [], "", "", "#000"

        def fs_get_view(xs_, ys_):
            if fs_state["vx0"] is not None:
                return fs_state["vx0"], fs_state["vx1"], fs_state["vy0"], fs_state["vy1"]
            return self._data_bounds(xs_, ys_)

        def fs_redraw(_event=None):
            xs_, ys_, xl_, yl_, c_ = fs_get_data()
            if len(xs_) < 2:
                return
            vx0, vx1, vy0, vy1 = fs_get_view(xs_, ys_)
            self._draw_curve_viewport(fs_canvas, xs_, ys_, xl_, yl_, c_, vx0, vx1, vy0, vy1)

        def fs_reset():
            fs_state["vx0"] = fs_state["vx1"] = None
            fs_state["vy0"] = fs_state["vy1"] = None
            fs_redraw()

        def fs_scroll(event):
            xs_, ys_, *_ = fs_get_data()
            if len(xs_) < 2:
                return
            vx0, vx1, vy0, vy1 = fs_get_view(xs_, ys_)
            factor = 0.8 if (getattr(event, 'delta', 0) > 0 or event.num == 4) else 1.25
            mx, my = self._pixel_to_data(fs_canvas, event.x, event.y, vx0, vx1, vy0, vy1)
            fs_state["vx0"] = mx - (mx - vx0) * factor
            fs_state["vx1"] = mx + (vx1 - mx) * factor
            fs_state["vy0"] = my - (my - vy0) * factor
            fs_state["vy1"] = my + (vy1 - my) * factor
            fs_redraw()

        def fs_pan_start(event):
            xs_, ys_, *_ = fs_get_data()
            vx0, vx1, vy0, vy1 = fs_get_view(xs_, ys_)
            fs_state["pan"] = (event.x, event.y, vx0, vx1, vy0, vy1)

        def fs_pan_move(event):
            if fs_state["pan"] is None:
                return
            sx, sy, vx0, vx1, vy0, vy1 = fs_state["pan"]
            ml, mr, mt, mb = self._canvas_margins()
            cw = fs_canvas.winfo_width()
            ch_ = fs_canvas.winfo_height()
            pw_ = cw - ml - mr
            ph_ = ch_ - mt - mb
            if pw_ <= 0 or ph_ <= 0:
                return
            dx = -(event.x - sx) / pw_ * (vx1 - vx0)
            dy = (event.y - sy) / ph_ * (vy1 - vy0)
            fs_state["vx0"] = vx0 + dx
            fs_state["vx1"] = vx1 + dx
            fs_state["vy0"] = vy0 + dy
            fs_state["vy1"] = vy1 + dy
            fs_redraw()

        def fs_pan_end(_event):
            fs_state["pan"] = None

        ttk.Label(top_bar, text="Affichage :").pack(side="left")
        for gt_ in ["I = f(t)", "Ewe = f(t)", "I = f(Ewe)", "Ewe = f(I)"]:
            ttk.Radiobutton(top_bar, text=gt_, value=gt_, variable=fs_graph_var,
                            command=lambda: (fs_reset())).pack(side="left", padx=4)
        ttk.Button(top_bar, text="Réinitialiser vue", command=fs_reset).pack(side="right", padx=4)

        fs_canvas = tk.Canvas(win, bg="white", highlightthickness=0)
        fs_canvas.pack(fill="both", expand=True)
        fs_canvas.bind("<Configure>", fs_redraw)
        fs_canvas.bind("<MouseWheel>", fs_scroll)
        fs_canvas.bind("<Button-4>", fs_scroll)
        fs_canvas.bind("<Button-5>", fs_scroll)
        fs_canvas.bind("<ButtonPress-1>", fs_pan_start)
        fs_canvas.bind("<B1-Motion>", fs_pan_move)
        fs_canvas.bind("<ButtonRelease-1>", fs_pan_end)

    # -----------------------------------------------------------------------
    # Plot update & draw
    # -----------------------------------------------------------------------

    def _update_plot(self):
        n = len(self._plot_t)
        self.data_count_var.set(f"Points : {n}")
        if n < 2:
            return
        self.progress_var.set(f"Acquisition… {n} points")

        xs, ys, xlabel, ylabel, color = self._get_current_data()
        vx0, vx1, vy0, vy1 = self._get_view(xs, ys)
        self._draw_curve_viewport(self.graph_canvas, xs, ys, xlabel, ylabel, color,
                                  vx0, vx1, vy0, vy1)

    def _draw_curve_viewport(self, canvas: tk.Canvas, xs: list[float], ys: list[float],
                             xlabel: str, ylabel: str, color: str,
                             x_min: float, x_max: float, y_min: float, y_max: float):
        canvas.delete("all")
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 50 or h < 50 or len(xs) < 2:
            return

        ml, mr, mt, mb = self._canvas_margins()
        pw = w - ml - mr
        ph = h - mt - mb
        if pw <= 0 or ph <= 0:
            return

        x_range = x_max - x_min if x_max != x_min else 1
        y_range = y_max - y_min if y_max != y_min else 1

        def tx(v):
            return ml + (v - x_min) / x_range * pw

        def ty(v):
            return mt + (1 - (v - y_min) / y_range) * ph

        # Background
        canvas.create_rectangle(ml, mt, w - mr, h - mb, fill="#fafafa", outline="")

        # Grid lines
        for frac in (0, 0.25, 0.5, 0.75, 1.0):
            yv = y_min + frac * y_range
            canvas.create_line(ml, ty(yv), w - mr, ty(yv), fill="#dcdcdc", dash=(2, 4))
            xv = x_min + frac * x_range
            canvas.create_line(tx(xv), mt, tx(xv), h - mb, fill="#dcdcdc", dash=(2, 4))

        # Axes
        canvas.create_line(ml, mt, ml, h - mb, fill="#333")
        canvas.create_line(ml, h - mb, w - mr, h - mb, fill="#333")

        # Labels
        canvas.create_text(w // 2, h - 5, text=xlabel, font=("", 10))
        canvas.create_text(14, h // 2, text=ylabel, font=("", 10), angle=90)

        # Title
        canvas.create_text(w // 2, 10, text=f"{ylabel} = f({xlabel})", font=("", 10, "bold"), fill="#333")

        # Tick labels
        for frac in (0, 0.25, 0.5, 0.75, 1.0):
            xv = x_min + frac * x_range
            canvas.create_text(tx(xv), h - mb + 16, text=f"{xv:.3g}", font=("", 8))
            yv = y_min + frac * y_range
            canvas.create_text(ml - 8, ty(yv), text=f"{yv:.3e}", font=("", 8), anchor="e")

        # Downsample
        max_pts = max(pw * 2, 100)
        if len(xs) > max_pts:
            step_d = max(len(xs) // max_pts, 1)
            xs_d = xs[::step_d]
            ys_d = ys[::step_d]
        else:
            xs_d = xs
            ys_d = ys

        # Draw crosses
        cross_size = 3
        for xv, yv in zip(xs_d, ys_d):
            cx, cy = tx(xv), ty(yv)
            if cx < ml - cross_size or cx > w - mr + cross_size:
                continue
            if cy < mt - cross_size or cy > h - mb + cross_size:
                continue
            canvas.create_line(cx - cross_size, cy - cross_size,
                               cx + cross_size, cy + cross_size, fill=color, width=1.5)
            canvas.create_line(cx - cross_size, cy + cross_size,
                               cx + cross_size, cy - cross_size, fill=color, width=1.5)

    # -----------------------------------------------------------------------
    # CSV Export (standard CA)
    # -----------------------------------------------------------------------

    def _on_export_csv(self):
        if not self._data_rows:
            messagebox.showinfo("Aucune donnée", "Pas de données à exporter.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("Tous", "*.*")],
            initialfile="ca_data.csv",
        )
        if not path:
            return
        try:
            with open(path, "w", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=["t", "Ewe", "I", "cycle"])
                writer.writeheader()
                writer.writerows(self._data_rows)
            self._log(f"Données exportées : {path} ({len(self._data_rows)} points)")
        except Exception as exc:
            messagebox.showerror("Erreur export", str(exc))

    # -----------------------------------------------------------------------
    # Cleanup
    # -----------------------------------------------------------------------

    def _on_close(self):
        self._on_stop_experiment()
        if self._experiment_thread is not None:
            self._experiment_thread.join(timeout=3)
        self._on_disconnect()
        self.destroy()


# ===========================================================================
# Entry point
# ===========================================================================


def main():
    app = BiologicPotentiostatTestApp()
    app.mainloop()


if __name__ == "__main__":
    main()
