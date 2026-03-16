"""
Interface principale combinée : Caméra Thorlabs + Moteurs Newport CONEX-CC.

Layout :
  - Barre de menus en haut (connexion moteurs, connexion caméra, affichage)
  - Zone centrale : aperçu caméra aussi grand que possible
  - Panneaux compacts en bas : contrôles moteur + contrôle du point
  - Barre de statut tout en bas
"""

import os
import queue
import sys
import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import messagebox, ttk, simpledialog

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageTk
except Exception as _dep_exc:
    print("Dépendances manquantes. Installer avec :")
    print("  python -m pip install numpy pillow")
    raise SystemExit(_dep_exc) from _dep_exc

from newport_conex_test_ui import (ConexAxis, ConexError, DEFAULT_DLL_CANDIDATES, load_conex_class,
                                   ABS_MIN_MM, ABS_MAX_MM)

# ─────────────────────────── Constantes ───────────────────────────

PREVIEW_POLL_MS = 15          # ~66 fps display polling
FPS_UPDATE_INTERVAL = 1.0     # seconds between FPS label updates

# ───────────────────────── Camera stream ──────────────────────────


class CameraStreamThread(threading.Thread):
    """Récupère les frames de la caméra dans un thread dédié."""

    def __init__(self, camera, image_queue, sensor_type_enum, mono_to_color_sdk_cls=None):
        super().__init__(daemon=True)
        self.camera = camera
        self.image_queue = image_queue
        self.sensor_type_enum = sensor_type_enum
        self.mono_to_color_sdk_cls = mono_to_color_sdk_cls
        self.stop_event = threading.Event()
        self.mono_to_color_sdk = None
        self.mono_to_color_processor = None
        self.bit_depth = int(camera.bit_depth)
        self.is_color = False
        self._setup_color_processing()

    def _setup_color_processing(self):
        if self.mono_to_color_sdk_cls is None:
            return
        if self.camera.camera_sensor_type != self.sensor_type_enum.BAYER:
            return
        try:
            self.mono_to_color_sdk = self.mono_to_color_sdk_cls()
            self.mono_to_color_processor = self.mono_to_color_sdk.create_mono_to_color_processor(
                self.sensor_type_enum.BAYER,
                self.camera.color_filter_array_phase,
                self.camera.get_color_correction_matrix(),
                self.camera.get_default_white_balance_matrix(),
                self.camera.bit_depth,
            )
            self.is_color = True
        except Exception:
            self.is_color = False

    def _frame_to_pil(self, frame):
        if self.is_color and self.mono_to_color_processor is not None:
            h, w = frame.image_buffer.shape[:2]
            rgb = self.mono_to_color_processor.transform_to_24(frame.image_buffer, w, h)
            return Image.fromarray(rgb.reshape(h, w, 3), mode="RGB")
        shift = max(self.bit_depth - 8, 0)
        return Image.fromarray((frame.image_buffer >> shift).astype(np.uint8), mode="L")

    def run(self):
        while not self.stop_event.is_set():
            try:
                frame = self.camera.get_pending_frame_or_null()
                if frame is None:
                    time.sleep(0.005)
                    continue
                image = self._frame_to_pil(frame)
                if self.image_queue.full():
                    try:
                        self.image_queue.get_nowait()
                    except queue.Empty:
                        pass
                self.image_queue.put_nowait(image)
            except Exception:
                time.sleep(0.02)

    def stop(self):
        self.stop_event.set()

    def dispose_processors(self):
        try:
            if self.mono_to_color_processor is not None:
                self.mono_to_color_processor.dispose()
        finally:
            self.mono_to_color_processor = None
        try:
            if self.mono_to_color_sdk is not None:
                self.mono_to_color_sdk.dispose()
        finally:
            self.mono_to_color_sdk = None


# ───────────────────── Dialogs de configuration ──────────────────


class MotorConfigDialog(tk.Toplevel):
    """Dialogue de configuration des moteurs Newport CONEX-CC."""

    def __init__(self, parent, app):
        super().__init__(parent)
        self.app = app
        self.title("Configuration Moteurs")
        self.geometry("660x280")
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._build()

    def _build(self):
        f = ttk.Frame(self, padding=14)
        f.pack(fill="both", expand=True)

        # DLL path
        ttk.Label(f, text="Chemin DLL Newport :").grid(row=0, column=0, sticky="w", pady=4)
        ttk.Entry(f, textvariable=self.app.motor_dll_var, width=60).grid(
            row=0, column=1, columnspan=3, sticky="ew", padx=6, pady=4
        )
        ttk.Button(f, text="Charger DLL", command=self._on_load).grid(row=0, column=4, padx=6, pady=4)

        # Scan
        ttk.Button(f, text="Scanner ports", command=self._on_scan).grid(row=1, column=4, padx=6, pady=4)

        # COM ports
        ttk.Label(f, text="Port COM X :").grid(row=1, column=0, sticky="w", pady=4)
        self.x_combo = ttk.Combobox(f, textvariable=self.app.x_port_var, width=20, state="readonly")
        self.x_combo.grid(row=1, column=1, sticky="w", pady=4)
        ttk.Label(f, text="Port COM Y :").grid(row=1, column=2, sticky="w", pady=4, padx=(12, 0))
        self.y_combo = ttk.Combobox(f, textvariable=self.app.y_port_var, width=20, state="readonly")
        self.y_combo.grid(row=1, column=3, sticky="w", pady=4)

        # Refresh combos with existing values
        if hasattr(self.app, '_motor_ports_list'):
            self.x_combo["values"] = self.app._motor_ports_list
            self.y_combo["values"] = self.app._motor_ports_list

        # Address + Timeout
        ttk.Label(f, text="Adresse :").grid(row=2, column=0, sticky="w", pady=4)
        ttk.Entry(f, textvariable=self.app.motor_addr_var, width=8).grid(row=2, column=1, sticky="w", pady=4)
        ttk.Label(f, text="Timeout (s) :").grid(row=2, column=2, sticky="w", pady=4, padx=(12, 0))
        ttk.Entry(f, textvariable=self.app.motor_timeout_var, width=8).grid(row=2, column=3, sticky="w", pady=4)

        # Actions
        btn_frame = ttk.Frame(f)
        btn_frame.grid(row=3, column=0, columnspan=5, pady=(16, 4))
        ttk.Button(btn_frame, text="Connecter moteurs", command=self._on_connect).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Initialiser (Home)", command=self._on_home).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Déconnecter", command=self._on_disconnect).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Fermer", command=self.destroy).pack(side="left", padx=6)

        # Status
        self.dlg_status = ttk.Label(f, text="", foreground="blue")
        self.dlg_status.grid(row=4, column=0, columnspan=5, sticky="w", pady=4)

    def _on_load(self):
        self.app.on_load_motor_dll()
        self.dlg_status.config(text="Chargement DLL...")

    def _on_scan(self):
        self.app.on_scan_motors(combo_x=self.x_combo, combo_y=self.y_combo)
        self.dlg_status.config(text="Scan en cours...")

    def _on_connect(self):
        self.app.on_connect_motors()
        self.dlg_status.config(text="Connexion...")

    def _on_home(self):
        self.app.on_initialize_motors()
        self.dlg_status.config(text="Initialisation (Home)...")

    def _on_disconnect(self):
        self.app.on_disconnect_motors()
        self.dlg_status.config(text="Déconnexion...")


class CameraConfigDialog(tk.Toplevel):
    """Dialogue de configuration de la caméra Thorlabs."""

    def __init__(self, parent, app):
        super().__init__(parent)
        self.app = app
        self.title("Configuration Caméra")
        self.geometry("660x300")
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        self._build()

    def _build(self):
        f = ttk.Frame(self, padding=14)
        f.pack(fill="both", expand=True)
 
        # DLL path
        ttk.Label(f, text="Chemin DLL natif :").grid(row=0, column=0, sticky="w", pady=4)
        ttk.Entry(f, textvariable=self.app.camera_dll_path_var, width=56).grid(
            row=0, column=1, columnspan=3, sticky="ew", padx=6, pady=4
        )
        ttk.Button(f, text="Charger SDK", command=self._on_load_sdk).grid(row=0, column=4, padx=6, pady=4)

        # Serial
        ttk.Label(f, text="Numéro de série :").grid(row=1, column=0, sticky="w", pady=4)
        self.serial_combo = ttk.Combobox(f, textvariable=self.app.serial_var, width=30, state="readonly")
        self.serial_combo.grid(row=1, column=1, sticky="w", pady=4)
        ttk.Button(f, text="Découvrir", command=self._on_discover).grid(row=1, column=4, padx=6, pady=4)

        # Refresh combo
        if hasattr(self.app, '_camera_serials_list'):
            self.serial_combo["values"] = self.app._camera_serials_list

        # Exposure + Gain
        ttk.Label(f, text="Exposition (s) :").grid(row=2, column=0, sticky="w", pady=4)
        ttk.Entry(f, textvariable=self.app.exposure_var, width=12).grid(row=2, column=1, sticky="w", pady=4)
        ttk.Label(f, text="Gain :").grid(row=2, column=2, sticky="w", pady=4, padx=(12, 0))
        ttk.Entry(f, textvariable=self.app.gain_var, width=12).grid(row=2, column=3, sticky="w", pady=4)

        # Gain info
        ttk.Label(f, textvariable=self.app.gain_info_var, foreground="gray").grid(
            row=3, column=0, columnspan=4, sticky="w", pady=2
        )

        # Actions
        btn_frame = ttk.Frame(f)
        btn_frame.grid(row=4, column=0, columnspan=5, pady=(14, 4))
        ttk.Button(btn_frame, text="Connecter", command=self._on_connect).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Déconnecter", command=self._on_disconnect).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Appliquer params", command=self._on_apply).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Fermer", command=self.destroy).pack(side="left", padx=6)

        # Status
        self.dlg_status = ttk.Label(f, text="", foreground="blue")
        self.dlg_status.grid(row=5, column=0, columnspan=5, sticky="w", pady=4)

    def _on_load_sdk(self):
        self.app.on_load_camera_sdk()
        self.dlg_status.config(text="SDK chargé" if self.app.TLCameraSDK else "Erreur SDK")

    def _on_discover(self):
        self.app.on_discover_camera(combo=self.serial_combo)
        self.dlg_status.config(text="Recherche terminée")

    def _on_connect(self):
        self.app.on_connect_camera()
        self.dlg_status.config(text="Connecté" if self.app.camera else "Erreur")

    def _on_disconnect(self):
        self.app.on_disconnect_camera()
        self.dlg_status.config(text="Déconnecté")

    def _on_apply(self):
        self.app.on_apply_camera_params()


# ─────────────────────── Application principale ──────────────────


class MainApp(tk.Tk):
    """Interface principale combinée Caméra + Moteurs."""

    def __init__(self):
        super().__init__()
        self.title("Plateforme de mesure – Caméra + Moteurs")
        self.geometry("1400x900")
        self.minsize(900, 600)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.project_root = Path(__file__).resolve().parent.parent

        # ── Motor state ──
        self.conex_class = None
        self.loaded_motor_dll = ""
        self.axis_x = None
        self.axis_y = None
        self._motor_ports_list = []

        # ── Camera state ──
        self.sdk = None
        self.camera = None
        self.stream_thread = None
        self.image_queue = queue.Queue(maxsize=2)
        self.preview_photo = None
        self._last_image = None
        self._preview_job = None
        self._frame_counter = 0
        self._fps_t0 = time.monotonic()
        self._last_frame_width = 0
        self._last_frame_height = 0
        self._camera_serials_list = []

        self.TLCameraSDK = None
        self.SENSOR_TYPE = None
        self.MonoToColorProcessorSDK = None

        # ── Variables ──
        default_motor_dll = ""
        for c in DEFAULT_DLL_CANDIDATES:
            if Path(c).exists():
                default_motor_dll = c
                break

        self.motor_dll_var = tk.StringVar(value=default_motor_dll)
        self.x_port_var = tk.StringVar()
        self.y_port_var = tk.StringVar()
        self.motor_addr_var = tk.StringVar(value="1")
        self.motor_timeout_var = tk.StringVar(value="30")
        self.step_x_var = tk.StringVar(value="0.05")
        self.step_y_var = tk.StringVar(value="0.05")
        self.jog_speed_var = tk.StringVar(value="0.5")  # mm/s pour le jog clavier continu
        self._jog_active: dict[str, bool] = {"X": False, "Y": False}

        self.serial_var = tk.StringVar(value="")
        self.exposure_var = tk.StringVar(value="0.01")  # en secondes
        self.gain_var = tk.StringVar(value="0")
        self.gain_info_var = tk.StringVar(value="Gain : inconnu")
        self.camera_dll_path_var = tk.StringVar(value=str(self._native_dll_dir()))

        self.status_var = tk.StringVar(value="Prêt")
        self.fps_var = tk.StringVar(value="FPS : –")

        # Point / laser overlay
        self.laser_x = 0
        self.laser_y = 0
        self.laser_initialized = False
        self.laser_coord_var = tk.StringVar(value="Point : X=– Y=–")
        self.laser_step_var = tk.StringVar(value="5")
        self.laser_size_var = tk.StringVar(value="6")

        # Objectif – préréglages position du point laser (px) + taille (px)
        # Format : {"x": int, "y": int, "size": int}
        self.OBJECTIVE_PRESETS = {
            "4x":  {"x": 2588, "y": 1350, "size": 32},
            "10x": {"x": 2588, "y": 1350, "size": 32},   # à calibrer
            "50x": {"x": 2588, "y": 1350, "size": 32},   # à calibrer
        }
        self.objective_var = tk.StringVar(value="4x")

        # Séquence de balayage
        self.seq_start_motor: tuple | None = None   # (x_mm, y_mm)
        self.seq_end_motor:   tuple | None = None
        self.seq_step_mm_var  = tk.StringVar(value="0.05")
        self.seq_duration_var = tk.StringVar(value="0.5")
        self.seq_start_lbl_var = tk.StringVar(value="Départ  : –")
        self.seq_end_lbl_var   = tk.StringVar(value="Arrivée : –")
        self.seq_status_var    = tk.StringVar(value="")
        self.seq_mode_var      = tk.StringVar(value="Linéaire")  # "Linéaire" ou "Rectangle"
        self._seq_stop_event   = threading.Event()
        self._seq_running      = False

        # Log visibility
        self._log_visible = tk.BooleanVar(value=True)
        self._controls_visible = tk.BooleanVar(value=True)

        self._build_ui()
        self._bind_keyboard_controls()
        self._log("Application prête.")

    # ═══════════════════════════ UI BUILD ═══════════════════════════

    def _build_ui(self):
        self._build_menubar()

        # Main container – grid layout so the preview never pushes fixed rows off screen
        self._main_frame = ttk.Frame(self)
        self._main_frame.pack(fill="both", expand=True)
        self._main_frame.rowconfigure(0, weight=1)   # preview expands
        self._main_frame.rowconfigure(1, weight=0)   # controls – fixed
        self._main_frame.rowconfigure(2, weight=0)   # log      – fixed
        self._main_frame.rowconfigure(3, weight=0)   # statusbar – fixed
        self._main_frame.columnconfigure(0, weight=1)

        # ── Camera preview (row 0 – expands to fill remaining space) ──
        self._preview_frame = ttk.Frame(self._main_frame, relief="sunken", borderwidth=1)
        self._preview_frame.grid(row=0, column=0, sticky="nsew", padx=2, pady=(2, 0))
        self._preview_frame.rowconfigure(0, weight=1)
        self._preview_frame.columnconfigure(0, weight=1)

        self.preview_label = ttk.Label(self._preview_frame, text="Aucune image – Connecter la caméra depuis le menu",
                                       anchor="center", background="#1a1a1a", foreground="#888888")
        self.preview_label.grid(row=0, column=0, sticky="nsew")
        self.preview_label.bind("<Configure>", self._on_preview_resize)

        # ── Bottom controls panel (row 1 – fixed height) ──
        self._controls_frame = ttk.Frame(self._main_frame)
        self._controls_frame.grid(row=1, column=0, sticky="ew", padx=2, pady=(2, 0))
        self._build_controls()

        # ── Log panel (row 2 – fixed height, toggleable) ──
        self._log_frame = ttk.LabelFrame(self._main_frame, text="Journal", padding=4)
        self._log_frame.grid(row=2, column=0, sticky="ew", padx=2, pady=(2, 0))
        self.log_text = tk.Text(self._log_frame, height=5, font=("Consolas", 9))
        log_scroll = ttk.Scrollbar(self._log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=log_scroll.set)
        log_scroll.pack(side="right", fill="y")
        self.log_text.pack(fill="both", expand=True)

        # ── Status bar (row 3 – fixed height) ──
        self._statusbar = ttk.Frame(self._main_frame, relief="sunken", borderwidth=1)
        self._statusbar.grid(row=3, column=0, sticky="ew", padx=2, pady=(2, 2))
        ttk.Label(self._statusbar, textvariable=self.status_var, foreground="blue",
                  width=40, anchor="w").pack(side="left", padx=6)
        ttk.Label(self._statusbar, textvariable=self.fps_var, width=14, anchor="w").pack(side="left", padx=6)
        ttk.Label(self._statusbar, textvariable=self.laser_coord_var, width=24, anchor="w").pack(side="left", padx=6)

        # Live / Stop buttons directly visible in status bar for quick access
        ttk.Button(self._statusbar, text="▶ Live", width=8, command=self.on_start_live).pack(side="right", padx=2)
        ttk.Button(self._statusbar, text="■ Stop", width=8, command=self.on_stop_live).pack(side="right", padx=2)

    def _build_menubar(self):
        menubar = tk.Menu(self)

        # ── Menu Moteurs ──
        motor_menu = tk.Menu(menubar, tearoff=0)
        motor_menu.add_command(label="Configuration moteurs…", command=self._show_motor_config)
        motor_menu.add_separator()
        motor_menu.add_command(label="Connecter moteurs", command=self.on_connect_motors)
        motor_menu.add_command(label="Initialiser (Home X+Y)", command=self.on_initialize_motors)
        motor_menu.add_command(label="Déconnecter moteurs", command=self.on_disconnect_motors)
        motor_menu.add_separator()
        motor_menu.add_command(label="STOP moteurs", command=self.on_stop_motors)
        menubar.add_cascade(label="Moteurs", menu=motor_menu)

        # ── Menu Caméra ──
        cam_menu = tk.Menu(menubar, tearoff=0)
        cam_menu.add_command(label="Configuration caméra…", command=self._show_camera_config)
        cam_menu.add_separator()
        cam_menu.add_command(label="Connecter caméra", command=self.on_connect_camera)
        cam_menu.add_command(label="Déconnecter caméra", command=self.on_disconnect_camera)
        cam_menu.add_separator()
        cam_menu.add_command(label="Démarrer flux live", command=self.on_start_live)
        cam_menu.add_command(label="Arrêter flux live", command=self.on_stop_live)
        cam_menu.add_separator()
        cam_menu.add_command(label="Appliquer paramètres", command=self.on_apply_camera_params)
        menubar.add_cascade(label="Caméra", menu=cam_menu)

        # ── Menu Affichage ──
        view_menu = tk.Menu(menubar, tearoff=0)
        view_menu.add_checkbutton(label="Afficher le journal", variable=self._log_visible,
                                  command=self._toggle_log)
        view_menu.add_checkbutton(label="Afficher les contrôles", variable=self._controls_visible,
                                  command=self._toggle_controls)
        menubar.add_cascade(label="Affichage", menu=view_menu)

        self.config(menu=menubar)

    def _build_controls(self):
        """Construit les petits panneaux compacts de contrôle en bas."""
        outer = self._controls_frame

        # ── Objectif + Point laser ──
        obj_box = ttk.LabelFrame(outer, text="Objectif / Laser", padding=4)
        obj_box.pack(side="left", fill="y", padx=(0, 4))

        ttk.Label(obj_box, text="Objectif").grid(row=0, column=0, sticky="w", padx=2)
        obj_combo = ttk.Combobox(obj_box, textvariable=self.objective_var,
                                 values=list(self.OBJECTIVE_PRESETS.keys()),
                                 state="readonly", width=5)
        obj_combo.grid(row=0, column=1, padx=2)
        obj_combo.bind("<<ComboboxSelected>>", lambda _: self.on_objective_change())
        ttk.Button(obj_box, text="Appliquer", command=self.on_objective_change, width=8
                   ).grid(row=0, column=2, padx=4)

        ttk.Label(obj_box, text="Pas (px)").grid(row=1, column=0, sticky="w", padx=2)
        ttk.Entry(obj_box, textvariable=self.laser_step_var, width=5).grid(row=1, column=1, padx=2)
        ttk.Button(obj_box, text="X−", width=3,
                   command=lambda: self.on_laser_move(-1, 0)).grid(row=1, column=2, padx=1)
        ttk.Button(obj_box, text="X+", width=3,
                   command=lambda: self.on_laser_move(+1, 0)).grid(row=1, column=3, padx=1)
        ttk.Button(obj_box, text="Y−", width=3,
                   command=lambda: self.on_laser_move(0, -1)).grid(row=1, column=4, padx=1)
        ttk.Button(obj_box, text="Y+", width=3,
                   command=lambda: self.on_laser_move(0, +1)).grid(row=1, column=5, padx=1)

        ttk.Label(obj_box, text="Taille (px)").grid(row=2, column=0, sticky="w", padx=2)
        ttk.Entry(obj_box, textvariable=self.laser_size_var, width=5).grid(row=2, column=1, padx=2)
        ttk.Button(obj_box, text="−", width=3,
                   command=lambda: self.on_laser_size(-1)).grid(row=2, column=2, padx=1)
        ttk.Button(obj_box, text="+", width=3,
                   command=lambda: self.on_laser_size(+1)).grid(row=2, column=3, padx=1)

        # ── Motor jog section ──
        motor_box = ttk.LabelFrame(outer, text="Moteurs", padding=4)
        motor_box.pack(side="left", fill="y", padx=(0, 4))

        ttk.Label(motor_box, text="Pas X (mm)").grid(row=0, column=0, sticky="w", padx=2)
        ttk.Entry(motor_box, textvariable=self.step_x_var, width=7).grid(row=0, column=1, padx=2)
        ttk.Button(motor_box, text="X −", width=4,
                   command=lambda: self.on_motor_jog("X", -1, False)).grid(row=0, column=2, padx=1)
        ttk.Button(motor_box, text="X +", width=4,
                   command=lambda: self.on_motor_jog("X", +1, False)).grid(row=0, column=3, padx=1)

        ttk.Label(motor_box, text="Pas Y (mm)").grid(row=1, column=0, sticky="w", padx=2)
        ttk.Entry(motor_box, textvariable=self.step_y_var, width=7).grid(row=1, column=1, padx=2)
        ttk.Button(motor_box, text="Y −", width=4,
                   command=lambda: self.on_motor_jog("Y", -1, False)).grid(row=1, column=2, padx=1)
        ttk.Button(motor_box, text="Y +", width=4,
                   command=lambda: self.on_motor_jog("Y", +1, False)).grid(row=1, column=3, padx=1)

        ttk.Label(motor_box, text="Plage : 0 – 25 mm", foreground="gray",
                  font=("Segoe UI", 8)).grid(row=2, column=0, columnspan=4, sticky="w", pady=(1, 0))

        ttk.Label(motor_box, text="Vit. clavier (mm/s)").grid(row=3, column=0, columnspan=2, sticky="w", padx=2, pady=(4, 0))
        ttk.Entry(motor_box, textvariable=self.jog_speed_var, width=7).grid(row=3, column=2, columnspan=2, sticky="w", padx=2, pady=(4, 0))
        ttk.Label(motor_box, text="Clavier continu : ← → ↑ ↓  | Echap = quitter champ", foreground="gray",
                  font=("Segoe UI", 8)).grid(row=4, column=0, columnspan=4, sticky="w", pady=(2, 0))

        # ── Séquence de balayage ──
        seq_box = ttk.LabelFrame(outer, text="Séquence balayage", padding=4)
        seq_box.pack(side="left", fill="y", padx=(0, 4))

        # Ligne 0 : labels départ / arrivée
        ttk.Label(seq_box, textvariable=self.seq_start_lbl_var, width=24,
                  foreground="#006600").grid(row=0, column=0, columnspan=4, sticky="w", padx=2)
        ttk.Label(seq_box, textvariable=self.seq_end_lbl_var, width=24,
                  foreground="#880000").grid(row=1, column=0, columnspan=4, sticky="w", padx=2)

        # Ligne 2 : mode + pas mm + durée
        ttk.Label(seq_box, text="Mode").grid(row=2, column=0, sticky="w", padx=2)
        ttk.Combobox(seq_box, textvariable=self.seq_mode_var,
                     values=["Linéaire", "Rectangle"],
                     state="readonly", width=9).grid(row=2, column=1, padx=2)
        ttk.Label(seq_box, text="Pas (mm)").grid(row=2, column=2, sticky="w", padx=(8, 2))
        ttk.Entry(seq_box, textvariable=self.seq_step_mm_var, width=7).grid(row=2, column=3, padx=2)

        # Ligne 3 : durée
        ttk.Label(seq_box, text="Durée/pt (s)").grid(row=3, column=0, sticky="w", padx=2)
        ttk.Entry(seq_box, textvariable=self.seq_duration_var, width=5).grid(row=3, column=1, padx=2)

        # Ligne 4 : boutons
        ttk.Button(seq_box, text="Set Départ",
                   command=self.on_set_seq_start).grid(row=4, column=0, padx=2, pady=(4, 0))
        ttk.Button(seq_box, text="Set Arrivée",
                   command=self.on_set_seq_end).grid(row=4, column=1, padx=2, pady=(4, 0))
        self._seq_run_btn = ttk.Button(seq_box, text="▶ Lancer",
                   command=self.on_run_sequence)
        self._seq_run_btn.grid(row=4, column=2, padx=2, pady=(4, 0))
        ttk.Button(seq_box, text="■ Stop",
                   command=self.on_stop_sequence).grid(row=4, column=3, padx=2, pady=(4, 0))

        # Ligne 5 : statut séquence
        ttk.Label(seq_box, textvariable=self.seq_status_var, foreground="blue",
                  font=("Segoe UI", 8)).grid(row=5, column=0, columnspan=4, sticky="w", pady=(2, 0))

        # ── Camera params quick section ──
        cam_box = ttk.LabelFrame(outer, text="Caméra rapide", padding=4)
        cam_box.pack(side="left", fill="y", padx=(0, 4))

        ttk.Label(cam_box, text="Expo (s)").grid(row=0, column=0, sticky="w", padx=2)
        ttk.Entry(cam_box, textvariable=self.exposure_var, width=9).grid(row=0, column=1, padx=2)
        ttk.Label(cam_box, text="Gain").grid(row=0, column=2, sticky="w", padx=(8, 2))
        ttk.Entry(cam_box, textvariable=self.gain_var, width=6).grid(row=0, column=3, padx=2)
        ttk.Button(cam_box, text="Appliquer", command=self.on_apply_camera_params).grid(
            row=0, column=4, padx=4)

        ttk.Label(cam_box, textvariable=self.gain_info_var, foreground="gray",
                  font=("Segoe UI", 8)).grid(row=1, column=0, columnspan=5, sticky="w", pady=(2, 0))

    # ── Toggle panels ──

    def _toggle_log(self):
        if self._log_visible.get():
            self._log_frame.grid(row=2, column=0, sticky="ew", padx=2, pady=(2, 0))
        else:
            self._log_frame.grid_remove()

    def _toggle_controls(self):
        if self._controls_visible.get():
            self._controls_frame.grid(row=1, column=0, sticky="ew", padx=2, pady=(2, 0))
        else:
            self._controls_frame.grid_remove()

    # ── Config dialogs ──

    def _show_motor_config(self):
        MotorConfigDialog(self, self)

    def _show_camera_config(self):
        CameraConfigDialog(self, self)

    # ═══════════════════════ KEYBOARD BINDINGS ═══════════════════════

    def _bind_keyboard_controls(self):
        self.bind_all("<Left>",           lambda e: self._on_arrow_press(e, "X", -1))
        self.bind_all("<Right>",          lambda e: self._on_arrow_press(e, "X", +1))
        self.bind_all("<Up>",             lambda e: self._on_arrow_press(e, "Y", +1))
        self.bind_all("<Down>",           lambda e: self._on_arrow_press(e, "Y", -1))
        self.bind_all("<KeyRelease-Left>",  lambda e: self._on_arrow_release(e, "X"))
        self.bind_all("<KeyRelease-Right>", lambda e: self._on_arrow_release(e, "X"))
        self.bind_all("<KeyRelease-Up>",    lambda e: self._on_arrow_release(e, "Y"))
        self.bind_all("<KeyRelease-Down>",  lambda e: self._on_arrow_release(e, "Y"))
        # Echap ou Entrée dans un champ texte : rendre le focus à la fenêtre principale
        self.bind_all("<Escape>", self._defocus_entry)
        self.bind_all("<Return>", self._defocus_entry)
        # Clic hors d'un champ texte : retrait automatique du focus
        self.bind_all("<Button-1>", self._on_global_click)

    def _defocus_entry(self, event=None):
        """Retire le focus d'un champ texte et le donne à la fenêtre principale."""
        wc = self.focus_get()
        if wc is not None:
            wc_class = wc.winfo_class()
            if wc_class in {"Entry", "TEntry", "Text", "Spinbox", "TSpinbox"}:
                self.focus_set()

    def _on_global_click(self, event):
        """Retire le focus d'un champ texte si on clique ailleurs."""
        wc = event.widget.winfo_class() if event.widget else ""
        if wc not in {"Entry", "TEntry", "Text", "Spinbox", "TSpinbox", "Combobox", "TCombobox"}:
            current_focus = self.focus_get()
            if current_focus is not None:
                cf_class = current_focus.winfo_class()
                if cf_class in {"Entry", "TEntry", "Text", "Spinbox", "TSpinbox"}:
                    self.focus_set()

    def _on_arrow_press(self, event, axis_name: str, direction: int):
        wc = event.widget.winfo_class() if event.widget else ""
        if wc in {"Entry", "TEntry", "Text"}:
            return
        self.on_motor_jog_start(axis_name, direction)
        return "break"

    def _on_arrow_release(self, event, axis_name: str):
        wc = event.widget.winfo_class() if event.widget else ""
        if wc in {"Entry", "TEntry", "Text"}:
            return
        self.on_motor_jog_stop(axis_name)
        return "break"

    # ═══════════════════════ HELPERS ═══════════════════════

    def _set_status(self, msg: str):
        self.status_var.set(msg)

    def _log(self, msg: str):
        stamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{stamp}] {msg}\n")
        self.log_text.see("end")

    def _run_async(self, title: str, worker):
        def run():
            try:
                self.after(0, lambda: self._set_status(f"{title}…"))
                worker()
                self.after(0, lambda: self._set_status(f"{title} : OK"))
            except Exception as exc:
                err = str(exc)
                self.after(0, lambda: self._set_status(f"{title} : ERREUR"))
                self.after(0, lambda m=err: self._log(f"{title} échoué : {m}"))
        threading.Thread(target=run, daemon=True).start()

    def _get_motor_timeout(self) -> float:
        v = float(self.motor_timeout_var.get())
        if v <= 0:
            raise ValueError
        return v

    def _get_motor_address(self) -> int:
        v = int(self.motor_addr_var.get())
        if v <= 0:
            raise ValueError
        return v

    def _axis_or_error(self, axis_name: str):
        axis = self.axis_x if axis_name == "X" else self.axis_y
        if axis is None or not axis.connected:
            raise ConexError(f"Axe {axis_name} non connecté")
        return axis

    # ═══════════════════════ MOTOR METHODS ═══════════════════════

    def on_load_motor_dll(self):
        path = self.motor_dll_var.get().strip()

        def worker():
            cls, loaded = load_conex_class(path)
            self.conex_class = cls
            self.loaded_motor_dll = loaded
            self.after(0, lambda: self._log(f"DLL moteur chargée : {loaded}"))
        self._run_async("Charger DLL moteur", worker)

    def on_scan_motors(self, combo_x=None, combo_y=None):
        def worker():
            if self.conex_class is None:
                raise ConexError("Charger la DLL moteur d'abord")
            tmp = self.conex_class()
            devices = sorted(set(str(p).strip() for p in (tmp.GetDevices() or []) if str(p).strip()))
            self._motor_ports_list = devices

            def apply():
                if combo_x is not None:
                    combo_x["values"] = devices
                if combo_y is not None:
                    combo_y["values"] = devices
                if devices and self.x_port_var.get() not in devices:
                    self.x_port_var.set(devices[0])
                if len(devices) > 1 and self.y_port_var.get() not in devices:
                    self.y_port_var.set(devices[1])
                elif devices and self.y_port_var.get() not in devices:
                    self.y_port_var.set(devices[0])
                self._log(f"Ports détectés : {devices}")
            self.after(0, apply)
        self._run_async("Scanner moteurs", worker)

    def on_connect_motors(self):
        try:
            addr = self._get_motor_address()
        except ValueError:
            messagebox.showerror("Erreur", "L'adresse du moteur doit être un entier positif")
            return
        x_port = self.x_port_var.get().strip()
        y_port = self.y_port_var.get().strip()
        if self.conex_class is None:
            messagebox.showerror("Erreur", "Charger la DLL moteur d'abord")
            return
        if not x_port or not y_port:
            messagebox.showerror("Erreur", "Sélectionner les deux ports COM (X et Y)")
            return
        if x_port == y_port:
            messagebox.showerror("Erreur", "Les ports X et Y doivent être différents")
            return

        def worker():
            self._disconnect_motors_impl(silent=True)
            # Mapping inversé (comme dans l'ancienne UI)
            mapped_x = y_port
            mapped_y = x_port
            ax = ConexAxis("X", self.conex_class, mapped_x, address=addr)
            ay = ConexAxis("Y", self.conex_class, mapped_y, address=addr)
            ax.open()
            ay.open()
            self.axis_x = ax
            self.axis_y = ay
            self.after(0, lambda: self._log(
                f"Moteurs connectés (COM inversé) X→{mapped_x}, Y→{mapped_y}, addr={addr}"))
        self._run_async("Connecter moteurs", worker)

    def on_initialize_motors(self):
        try:
            timeout_s = max(60.0, self._get_motor_timeout())
        except ValueError:
            messagebox.showerror("Erreur", "Le timeout doit être un nombre positif")
            return

        def worker():
            for name in ("X", "Y"):
                axis = self._axis_or_error(name)
                self.after(0, lambda n=name: self._log(f"Initialisation {n} : OR en cours"))
                axis.home(timeout_s=timeout_s)
                self.after(0, lambda n=name: self._log(f"Initialisation {n} : OR terminé"))
        self._run_async("Initialiser moteurs", worker)

    def on_stop_motors(self):
        def worker():
            for name in ("X", "Y"):
                axis = self.axis_x if name == "X" else self.axis_y
                if axis is not None and axis.connected:
                    axis.stop()
            self.after(0, lambda: self._log("STOP X+Y envoyé"))
        self._run_async("Stop moteurs", worker)

    def _disconnect_motors_impl(self, silent=False):
        for axis in (self.axis_x, self.axis_y):
            if axis is None:
                continue
            try:
                axis.close()
            except Exception as exc:
                if not silent:
                    self.after(0, lambda m=str(exc): self._log(f"Avert. déconnexion moteur : {m}"))
        self.axis_x = None
        self.axis_y = None

    def on_disconnect_motors(self):
        self._run_async("Déconnecter moteurs", lambda: self._disconnect_motors_impl(silent=False))

    def on_motor_jog(self, axis_name: str, direction: int, from_keyboard: bool):
        step_var = self.step_x_var if axis_name == "X" else self.step_y_var
        try:
            timeout_s = self._get_motor_timeout()
            step = float(step_var.get())
            if step <= 0:
                raise ValueError
            delta = step if direction > 0 else -step
        except ValueError:
            if not from_keyboard:
                messagebox.showerror("Erreur", f"Le pas {axis_name} doit être un nombre positif")
            else:
                self._log(f"Pas {axis_name} invalide")
            return

        def worker():
            axis = self._axis_or_error(axis_name)
            axis.move_relative(delta, timeout_s=timeout_s)
            self.after(0, lambda: self._log(f"Jog {axis_name} : {delta:+.4f} mm"))
        self._run_async(f"Jog {axis_name}", worker)

    def on_motor_jog_start(self, axis_name: str, direction: int):
        """Démarre un déplacement continu (appui maintenu sur une flèche).
        
        Règle la vitesse via VA puis envoie PA vers la limite de course dans la
        direction demandée. La répétition auto des touches est ignorée : seul le
        premier appui (quand _jog_active est False) déclenche le mouvement.
        """
        if self._jog_active.get(axis_name, False):
            return  # auto-repeat → ignoré
        axis = self.axis_x if axis_name == "X" else self.axis_y
        if axis is None or not axis.connected:
            return
        try:
            speed = float(self.jog_speed_var.get())
            if speed <= 0:
                raise ValueError
        except ValueError:
            self._log("Vitesse de jog invalide (doit être > 0)")
            return
        self._jog_active[axis_name] = True
        limit = ABS_MAX_MM if direction > 0 else ABS_MIN_MM

        def worker():
            try:
                axis.set_velocity(speed)
                axis.move_absolute_no_wait(limit)
            except Exception as exc:
                self.after(0, lambda m=str(exc): self._log(f"Jog {axis_name} démarrage erreur : {m}"))
                self._jog_active[axis_name] = False
        threading.Thread(target=worker, daemon=True).start()

    def on_motor_jog_stop(self, axis_name: str):
        """Stoppe le déplacement continu (relâchement de la touche).
        
        Après le stop, remet la vitesse à sa valeur nominale pour que les
        mouvements suivants (balayage, jog pas-à-pas) ne soient pas affectés.
        """
        if not self._jog_active.get(axis_name, False):
            return
        self._jog_active[axis_name] = False
        axis = self.axis_x if axis_name == "X" else self.axis_y
        if axis is None or not axis.connected:
            return

        def worker():
            try:
                axis.stop()
            except Exception as exc:
                self.after(0, lambda m=str(exc): self._log(f"Jog {axis_name} arrêt erreur : {m}"))
        threading.Thread(target=worker, daemon=True).start()

    # ═══════════════════════ OBJECTIF / PRESET LASER ═══════════════════════

    def on_objective_change(self):
        """Applique le préréglage laser de l'objectif sélectionné."""
        name = self.objective_var.get()
        preset = self.OBJECTIVE_PRESETS.get(name)
        if not preset:
            return
        self.laser_x = preset["x"]
        self.laser_y = preset["y"]
        self.laser_size_var.set(str(preset["size"]))
        self.laser_initialized = True
        self._update_laser_label()
        if self._last_image is not None:
            self._render_preview(self._last_image)
        self._log(f"Objectif {name} appliqué : point laser X={preset['x']} Y={preset['y']} taille={preset['size']}px")

    # ═══════════════════════ SÉQUENCE BALAYAGE ═══════════════════════

    def _read_motor_positions(self) -> tuple[float, float]:
        """Lit la position actuelle des deux axes (mm). Lève ConexError si non connecté."""
        ax = self._axis_or_error("X")
        ay = self._axis_or_error("Y")
        return ax.read_position(), ay.read_position()

    def on_set_seq_start(self):
        """Enregistre la position moteur actuelle comme point de départ de la séquence."""
        def worker():
            try:
                x, y = self._read_motor_positions()
                self.seq_start_motor = (x, y)
                lbl = f"Départ  : X={x:.4f}  Y={y:.4f} mm"
                self.after(0, lambda: self.seq_start_lbl_var.set(lbl))
                self.after(0, lambda: self._log(f"Point de départ enregistré : X={x:.4f} Y={y:.4f} mm"))
            except ConexError as exc:
                self.after(0, lambda: messagebox.showerror("Erreur", str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def on_set_seq_end(self):
        """Enregistre la position moteur actuelle comme point d'arrivée de la séquence."""
        def worker():
            try:
                x, y = self._read_motor_positions()
                self.seq_end_motor = (x, y)
                lbl = f"Arrivée : X={x:.4f}  Y={y:.4f} mm"
                self.after(0, lambda: self.seq_end_lbl_var.set(lbl))
                self.after(0, lambda: self._log(f"Point d'arrivée enregistré : X={x:.4f} Y={y:.4f} mm"))
            except ConexError as exc:
                self.after(0, lambda: messagebox.showerror("Erreur", str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def on_stop_sequence(self):
        """Arrête la séquence en cours."""
        self._seq_stop_event.set()
        self.after(0, lambda: self.seq_status_var.set("Arrêt demandé…"))

    @staticmethod
    def _build_waypoints_linear(x0, y0, x1, y1, step_mm) -> list:
        """Points en ligne droite de (x0,y0) à (x1,y1), espacés de step_mm."""
        import math
        dist = math.hypot(x1 - x0, y1 - y0)
        if dist < 1e-9:
            return [(x0, y0)]
        n = max(1, round(dist / step_mm))
        return [
            (x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n)
            for i in range(n + 1)
        ]

    @staticmethod
    def _build_waypoints_rect(x0, y0, x1, y1, step_mm) -> list:
        """
        Balayage raster en serpentin sur le rectangle défini par les coins (x0,y0) et (x1,y1).

        - Les lignes progressent dans la direction Y (de y0 vers y1).
        - La première ligne va de x0 vers x1.
        - Les lignes suivantes alternent le sens X (serpentin).
        - Le pas est le même dans les deux directions.
        """
        import math
        dx = x1 - x0
        dy = y1 - y0
        n_cols = max(1, round(abs(dx) / step_mm))
        n_rows = max(1, round(abs(dy) / step_mm))
        waypoints = []
        for row in range(n_rows + 1):
            y = y0 + dy * row / n_rows
            if row % 2 == 0:  # gauche → droite (sens naturel)
                x_range = [x0 + dx * col / n_cols for col in range(n_cols + 1)]
            else:             # droite → gauche (retour serpentin)
                x_range = [x0 + dx * col / n_cols for col in range(n_cols, -1, -1)]
            for x in x_range:
                waypoints.append((x, y))
        return waypoints

    def on_run_sequence(self):
        """Lance la séquence de balayage (linéaire ou rectangulaire) sur un thread dédié."""
        if self._seq_running:
            messagebox.showwarning("Séquence", "Une séquence est déjà en cours.")
            return
        if self.seq_start_motor is None:
            messagebox.showerror("Séquence", "Définir d'abord le point de départ.")
            return
        if self.seq_end_motor is None:
            messagebox.showerror("Séquence", "Définir d'abord le point d'arrivée.")
            return
        try:
            step_mm  = float(self.seq_step_mm_var.get())
            duration = float(self.seq_duration_var.get())
            timeout  = self._get_motor_timeout()
            if step_mm <= 0 or duration <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("Séquence", "Pas (mm) et Durée/pt doivent être des nombres positifs.")
            return

        x0, y0 = self.seq_start_motor
        x1, y1 = self.seq_end_motor
        mode = self.seq_mode_var.get()

        if mode == "Rectangle":
            waypoints = self._build_waypoints_rect(x0, y0, x1, y1, step_mm)
            mode_lbl = "rectangle (serpentin)"
        else:
            waypoints = self._build_waypoints_linear(x0, y0, x1, y1, step_mm)
            mode_lbl = "linéaire"

        if not waypoints:
            messagebox.showwarning("Séquence", "Départ et arrivée sont au même endroit.")
            return

        self._seq_stop_event.clear()
        self._seq_running = True
        total = len(waypoints)
        self.after(0, lambda: self.seq_status_var.set(f"0 / {total} points"))
        self._log(
            f"Séquence {mode_lbl} : ({x0:.4f},{y0:.4f}) → ({x1:.4f},{y1:.4f}) "
            f"| {total} points | pas={step_mm} mm | {duration}s/pt"
        )

        def worker():
            try:
                ax = self._axis_or_error("X")
                ay = self._axis_or_error("Y")

                # ── Phase 0 : mise en position sur le point de départ ──
                # Ce déplacement peut être long (grand saut) → pas de délai de mesure.
                start_x, start_y = waypoints[0]
                self.after(0, lambda: self.seq_status_var.set("Mise en position…"))
                self.after(0, lambda: self._log(
                    f"Mise en position vers départ ({start_x:.4f}, {start_y:.4f}) mm…"))
                ax.move_absolute_no_wait(start_x)
                ay.move_absolute_no_wait(start_y)
                ax.wait_done(timeout_s=timeout)
                ay.wait_done(timeout_s=timeout)
                if self._seq_stop_event.is_set():
                    self.after(0, lambda: self.seq_status_var.set("Arrêté."))
                    self.after(0, lambda: self._log("Séquence arrêtée par l'utilisateur."))
                    return
                self.after(0, lambda: self._log("Point de départ atteint. Début du balayage."))

                # ── Phase 1 : balayage pas à pas (délai de mesure à chaque point) ──
                for idx, (wx, wy) in enumerate(waypoints):
                    if self._seq_stop_event.is_set():
                        self.after(0, lambda: self.seq_status_var.set("Arrêté."))
                        self.after(0, lambda: self._log("Séquence arrêtée par l'utilisateur."))
                        return
                    lbl = f"{idx + 1} / {total} – ({wx:.4f}, {wy:.4f})"
                    self.after(0, lambda l=lbl: self.seq_status_var.set(l))
                    ax.move_absolute_no_wait(wx)
                    ay.move_absolute_no_wait(wy)
                    ax.wait_done(timeout_s=timeout)
                    ay.wait_done(timeout_s=timeout)
                    if self._seq_stop_event.is_set():
                        self.after(0, lambda: self.seq_status_var.set("Arrêté."))
                        return
                    time.sleep(duration)
                self.after(0, lambda: self.seq_status_var.set(f"Terminé ({total} points)."))
                self.after(0, lambda: self._log("Séquence terminée."))
            except ConexError as exc:
                msg = str(exc)
                self.after(0, lambda: self.seq_status_var.set(f"ERREUR : {msg}"))
                self.after(0, lambda: self._log(f"Séquence échouée : {msg}"))
            except Exception as exc:
                msg = str(exc)
                self.after(0, lambda: self.seq_status_var.set(f"ERREUR : {msg}"))
                self.after(0, lambda: self._log(f"Séquence exception : {msg}"))
            finally:
                self._seq_running = False

        threading.Thread(target=worker, daemon=True).start()

    # ═══════════════════════ CAMERA METHODS ═══════════════════════

    def _native_dll_dir(self) -> Path:
        is64 = sys.maxsize > 2**32
        sub = "Native_64_lib" if is64 else "Native_32_lib"
        candidates = [
            Path(r"C:\Program Files\Thorlabs\ThorImageCAM"),
            Path(r"C:\Program Files\Thorlabs\ThorImageCAM") / "dlls" / ("64_lib" if is64 else "32_lib"),
            Path(r"C:\Program Files\Thorlabs\ThorImageCAM") / "Native Toolkit" / "dlls" / sub,
            self.project_root / "Camera" / "SDK" / "Native Toolkit" / "dlls" / sub,
        ]
        for c in candidates:
            if c.exists():
                return c
        return candidates[0]

    @staticmethod
    def _resolve_camera_dll_directory(path_hint: Path) -> Path:
        dll_name = "thorlabs_tsi_camera_sdk.dll"
        if path_hint.is_file() and path_hint.name.lower() == dll_name:
            return path_hint.parent
        if not path_hint.exists():
            raise RuntimeError(f"Dossier DLL introuvable : {path_hint}")
        if (path_hint / dll_name).exists():
            return path_hint
        matches = list(path_hint.rglob(dll_name))
        if matches:
            return matches[0].parent
        raise RuntimeError(f"'{dll_name}' introuvable dans : {path_hint}")

    def _python_sdk_zip_subdir(self) -> Path:
        zp = self.project_root / "Camera" / "SDK" / "Python Toolkit" / "thorlabs_tsi_camera_python_sdk_package.zip"
        return Path(str(zp) + "\\thorlabs_tsi_sdk-0.0.8")

    def _configure_camera_runtime_paths(self):
        hint = Path(self.camera_dll_path_var.get().strip()) if self.camera_dll_path_var.get().strip() else self._native_dll_dir()
        native_dir = self._resolve_camera_dll_directory(hint)
        self.camera_dll_path_var.set(str(native_dir))
        os.environ["PATH"] = str(native_dir) + os.pathsep + os.environ.get("PATH", "")
        try:
            os.add_dll_directory(str(native_dir))
        except AttributeError:
            pass
        zip_sub = self._python_sdk_zip_subdir()
        if zip_sub.exists():
            sys.path.insert(0, str(zip_sub))

    def _load_camera_sdk_modules(self):
        self._configure_camera_runtime_paths()
        try:
            from thorlabs_tsi_sdk.tl_camera import TLCameraSDK
            from thorlabs_tsi_sdk.tl_camera_enums import SENSOR_TYPE
            try:
                from thorlabs_tsi_sdk.tl_mono_to_color_processor import MonoToColorProcessorSDK
            except Exception:
                MonoToColorProcessorSDK = None
        except Exception as exc:
            raise RuntimeError(
                "Impossible d'importer le SDK Python Thorlabs.\n"
                "python -m pip install numpy pillow\n"
                "python -m pip install \"Camera/SDK/Python Toolkit/thorlabs_tsi_camera_python_sdk_package.zip\"\n"
                f"Erreur : {exc}"
            ) from exc
        self.TLCameraSDK = TLCameraSDK
        self.SENSOR_TYPE = SENSOR_TYPE
        self.MonoToColorProcessorSDK = MonoToColorProcessorSDK

    def on_load_camera_sdk(self):
        try:
            self._load_camera_sdk_modules()
            self._set_status("SDK caméra chargé")
            self._log("SDK caméra chargé.")
        except Exception as exc:
            messagebox.showerror("Erreur SDK caméra", str(exc))

    def on_discover_camera(self, combo=None):
        try:
            if self.TLCameraSDK is None:
                self._load_camera_sdk_modules()
            if self.sdk is None:
                self.sdk = self.TLCameraSDK()
            serials = list(self.sdk.discover_available_cameras())
            self._camera_serials_list = serials
            if combo is not None:
                combo["values"] = serials
            if serials:
                self.serial_var.set(serials[0])
                self._set_status(f"{len(serials)} caméra(s) détectée(s)")
                self._log(f"Caméras détectées : {serials}")
            else:
                self.serial_var.set("")
                self._set_status("Aucune caméra détectée")
                self._log("Aucune caméra détectée.")
        except Exception as exc:
            messagebox.showerror("Erreur découverte caméra", str(exc))

    def on_connect_camera(self):
        try:
            if self.sdk is None:
                self.on_discover_camera()
            serial = self.serial_var.get().strip()
            if not serial:
                raise RuntimeError("Aucune caméra sélectionnée")
            if self.camera is not None:
                self.on_disconnect_camera()
            self.camera = self.sdk.open_camera(serial)
            self.exposure_var.set(str(round(self.camera.exposure_time_us / 1_000_000, 6)))
            self._refresh_gain_from_camera()
            self._set_status(f"Caméra connectée : {serial}")
            self._log(
                f"Caméra connectée modèle={self.camera.model} "
                f"rés={self.camera.image_width_pixels}×{self.camera.image_height_pixels}"
            )
        except Exception as exc:
            messagebox.showerror("Erreur connexion caméra", str(exc))

    def _refresh_gain_from_camera(self):
        if self.camera is None:
            self.gain_info_var.set("Gain : inconnu")
            return
        try:
            gr = self.camera.gain_range
            gmin, gmax = int(gr.min), int(gr.max)
            if gmax <= 0:
                self.gain_info_var.set("Gain : non supporté")
                self.gain_var.set("0")
                return
            cur = int(self.camera.gain)
            self.gain_var.set(str(cur))
            try:
                db = float(self.camera.convert_gain_to_decibels(cur))
                self.gain_info_var.set(f"Gain : {gmin}..{gmax} ({db:.2f} dB)")
            except Exception:
                self.gain_info_var.set(f"Gain : {gmin}..{gmax}")
        except Exception:
            self.gain_info_var.set("Gain : indisponible")

    def on_apply_camera_params(self):
        try:
            self._apply_camera_params_checked()
            self._refresh_gain_from_camera()
            self._log("Paramètres caméra appliqués.")
            self._set_status("Paramètres caméra appliqués")
        except ValueError:
            messagebox.showerror("Erreur", "Exposition / Gain doivent être des nombres valides")
        except Exception as exc:
            messagebox.showerror("Erreur paramètres caméra", str(exc))

    def _apply_camera_params_checked(self):
        if self.camera is None:
            raise RuntimeError("Caméra non connectée")
        exp_s = float(self.exposure_var.get())
        if exp_s <= 0:
            raise ValueError
        exp_us = int(round(exp_s * 1_000_000))
        if exp_us <= 0:
            raise ValueError
        self.camera.exposure_time_us = exp_us
        gr = self.camera.gain_range
        gmax = int(gr.max)
        if gmax > 0:
            gval = int(float(self.gain_var.get()))
            gmin = int(gr.min)
            if gval < gmin or gval > gmax:
                raise RuntimeError(f"Le gain doit être entre [{gmin}, {gmax}]")
            self.camera.gain = gval

    def on_start_live(self):
        try:
            if self.camera is None:
                raise RuntimeError("Caméra non connectée")
            if self.stream_thread is not None:
                return
            self._apply_camera_params_checked()
            self._refresh_gain_from_camera()
            self.camera.frames_per_trigger_zero_for_unlimited = 0
            self.camera.image_poll_timeout_ms = 0
            self.camera.arm(2)
            self.camera.issue_software_trigger()
            self.image_queue = queue.Queue(maxsize=2)
            self.stream_thread = CameraStreamThread(
                camera=self.camera,
                image_queue=self.image_queue,
                sensor_type_enum=self.SENSOR_TYPE,
                mono_to_color_sdk_cls=self.MonoToColorProcessorSDK,
            )
            self.stream_thread.start()
            self._frame_counter = 0
            self._fps_t0 = time.monotonic()
            self._set_status("Flux live en cours")
            self._log("Flux live démarré.")
            self._schedule_preview_update()
        except ValueError:
            messagebox.showerror("Erreur", "Paramètres exposition / gain invalides")
        except Exception as exc:
            messagebox.showerror("Erreur démarrage live", str(exc))

    def on_stop_live(self):
        if self.stream_thread is None:
            return
        try:
            self.stream_thread.stop()
            self.stream_thread.join(timeout=2.0)
            self.stream_thread.dispose_processors()
        finally:
            self.stream_thread = None
        if self._preview_job is not None:
            self.after_cancel(self._preview_job)
            self._preview_job = None
        try:
            if self.camera is not None:
                self.camera.disarm()
        except Exception:
            pass
        self._set_status("Flux live arrêté")
        self._log("Flux live arrêté.")

    def on_disconnect_camera(self):
        self.on_stop_live()
        if self.camera is not None:
            try:
                self.camera.dispose()
            finally:
                self.camera = None
        self._set_status("Caméra déconnectée")
        self._log("Caméra déconnectée.")

    # ═══════════════════ POINT / LASER OVERLAY ═══════════════════

    def _update_laser_label(self):
        if not self.laser_initialized:
            self.laser_coord_var.set("Point : X=– Y=–")
            return
        self.laser_coord_var.set(f"Point : X={self.laser_x} Y={self.laser_y}")

    def _clamp_laser_to_frame(self):
        if self._last_frame_width <= 0 or self._last_frame_height <= 0:
            return
        self.laser_x = max(0, min(self._last_frame_width - 1, int(self.laser_x)))
        self.laser_y = max(0, min(self._last_frame_height - 1, int(self.laser_y)))

    def _init_laser_if_needed(self):
        """Initialise la position du point laser à partir du preset de l'objectif actif."""
        if self.laser_initialized:
            return
        if self._last_frame_width <= 0 or self._last_frame_height <= 0:
            return
        preset = self.OBJECTIVE_PRESETS.get(self.objective_var.get(), {})
        self.laser_x = preset.get("x", self._last_frame_width // 2)
        self.laser_y = preset.get("y", self._last_frame_height // 2)
        self.laser_size_var.set(str(preset.get("size", 32)))
        self.laser_initialized = True
        self._update_laser_label()

    def _draw_laser_overlay(self, image: Image.Image) -> Image.Image:
        self._init_laser_if_needed()
        if not self.laser_initialized:
            return image
        try:
            radius = int(float(self.laser_size_var.get()))
        except ValueError:
            radius = 6
        radius = max(radius, 1)
        self._clamp_laser_to_frame()
        if image.mode != "RGB":
            image = image.convert("RGB")
        draw = ImageDraw.Draw(image)
        x, y = int(self.laser_x), int(self.laser_y)
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), outline=(255, 0, 0), width=2)
        draw.ellipse((x - 1, y - 1, x + 1, y + 1), fill=(255, 0, 0))
        return image

    def on_laser_move(self, dx: int, dy: int):
        try:
            step = int(float(self.laser_step_var.get()))
        except ValueError:
            messagebox.showerror("Erreur", "Le pas du point doit être un entier positif")
            return
        if step <= 0:
            messagebox.showerror("Erreur", "Le pas du point doit être un entier positif")
            return
        self._init_laser_if_needed()
        if not self.laser_initialized:
            self._log("Démarrer le flux live pour initialiser les coordonnées")
            return
        self.laser_x += int(dx * step)
        self.laser_y += int(dy * step)
        self._clamp_laser_to_frame()
        self._update_laser_label()
        if self._last_image is not None:
            self._render_preview(self._last_image)

    def on_laser_size(self, delta: int):
        try:
            size = int(float(self.laser_size_var.get()))
        except ValueError:
            size = 6
        size = max(1, size + int(delta))
        self.laser_size_var.set(str(size))
        if self._last_image is not None:
            self._render_preview(self._last_image)

    # ═══════════════════════ PREVIEW ═══════════════════════

    def _schedule_preview_update(self):
        if self._preview_job is None:
            self._preview_loop()

    def _fit_image_to_preview(self, image: Image.Image) -> Image.Image:
        tw = max(self.preview_label.winfo_width(), 1)
        th = max(self.preview_label.winfo_height(), 1)
        sw, sh = image.size
        if sw <= 0 or sh <= 0:
            return image
        scale = min(tw / sw, th / sh)
        if scale <= 0:
            return image
        nw = max(int(sw * scale), 1)
        nh = max(int(sh * scale), 1)
        if nw == sw and nh == sh:
            return image
        try:
            resample = Image.Resampling.LANCZOS
        except AttributeError:
            resample = Image.LANCZOS
        return image.resize((nw, nh), resample)

    def _render_preview(self, image: Image.Image):
        display = self._draw_laser_overlay(image.copy())
        fitted = self._fit_image_to_preview(display)
        self.preview_photo = ImageTk.PhotoImage(image=fitted)
        self.preview_label.configure(image=self.preview_photo, text="")

    def _on_preview_resize(self, _event):
        if self._last_image is not None:
            self._render_preview(self._last_image)

    def _preview_loop(self):
        self._preview_job = None
        if self.stream_thread is None:
            return
        try:
            image = self.image_queue.get_nowait()
            self._last_image = image
            self._last_frame_width, self._last_frame_height = image.size
            self._init_laser_if_needed()
            self._update_laser_label()
            self._render_preview(image)
            self._frame_counter += 1
            elapsed = time.monotonic() - self._fps_t0
            if elapsed >= FPS_UPDATE_INTERVAL:
                fps = self._frame_counter / elapsed
                self.fps_var.set(f"FPS : {fps:.1f}")
                self._frame_counter = 0
                self._fps_t0 = time.monotonic()
        except queue.Empty:
            pass
        self._preview_job = self.after(PREVIEW_POLL_MS, self._preview_loop)

    # ═══════════════════════ CLEANUP ═══════════════════════

    def _dispose_camera_sdk(self):
        if self.sdk is not None:
            try:
                self.sdk.dispose()
            finally:
                self.sdk = None

    def _on_close(self):
        try:
            self.on_disconnect_camera()
            self._disconnect_motors_impl(silent=True)
        finally:
            self._dispose_camera_sdk()
            self.destroy()


# ──────────────────────── Entry point ────────────────────────

def main():
    app = MainApp()
    app.mainloop()


if __name__ == "__main__":
    main()
