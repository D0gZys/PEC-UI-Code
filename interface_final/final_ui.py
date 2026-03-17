from __future__ import annotations

import csv
import os
import sys
import threading
import time
from pathlib import Path

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

PROJECT_ROOT = Path(__file__).resolve().parent.parent
MAINUI_DIR = PROJECT_ROOT / "MainUI"
if str(MAINUI_DIR) not in sys.path:
    sys.path.insert(0, str(MAINUI_DIR))

import main_ui as main_ui_mod
from main_ui import MainApp
import potentiostat_support as ps


class IntegratedMeasurementApp(MainApp):
    def __init__(self):
        super().__init__()
        self.title("Interface finale - camera, moteurs, potentiostat")
        self.geometry("1550x960")
        self.minsize(1200, 760)
        self._update_measure_zone_label()

    def _init_pot_state(self):
        if getattr(self, "_pot_ready", False):
            return
        self._pot_ready = True
        self.pot_api = None
        self.pot_connection_id = None
        self.pot_board_type = None
        self._pot_thread = None
        self._pot_stop_event = threading.Event()
        self._pot_data_rows = []
        self._pot_plot_t = []
        self._pot_plot_I = []
        self._pot_plot_E = []
        self._pot_matrix = []
        self._pot_rows = 0
        self._pot_cols = 0
        self._pot_index = 0
        self._pot_running = False
        self.pot_dll_path_var = tk.StringVar(value=ps.default_dll_path())
        self.pot_address_var = tk.StringVar(value="169.254.3.150")
        self.pot_channel_var = tk.IntVar(value=1)
        self.pot_status_var = tk.StringVar(value="Deconnecte")
        self.pot_voltage_var = tk.StringVar(value="0.500")
        self.pot_duration_var = tk.StringVar(value="")
        self.pot_vs_var = tk.StringVar(value="Ref")
        self.pot_record_dt_var = tk.StringVar(value="0.1000")
        self.pot_e_range_var = tk.StringVar(value="-2,5 V; 2,5 V")
        self.pot_i_range_var = tk.StringVar(value="Auto")
        self.pot_bw_var = tk.StringVar(value="8")
        self.pot_cycles_var = tk.StringVar(value="0")
        self.pot_graph_type_var = tk.StringVar(value="I = f(t)")
        self.pot_progress_var = tk.StringVar(value="")
        self.pot_zone_var = tk.StringVar(value="Zone : non definie")
        self.pot_count_var = tk.StringVar(value="Points : 0")
        self.seq_pick_status_var = tk.StringVar(value="Zone image : inactive")
        self.seq_pick_btn_var = tk.StringVar(value="Zone image")
        self._seq_select_armed = False
        self._seq_select_first_frame = None
        self._seq_rect_start_frame = None
        self._seq_rect_end_frame = None
        self._seq_select_base_motor = None

    def _build_ui(self):
        self._init_pot_state()
        self._build_menubar()

        self._main_frame = ttk.Frame(self)
        self._main_frame.pack(fill="both", expand=True)
        self._main_frame.rowconfigure(0, weight=1)
        self._main_frame.rowconfigure(1, weight=0)
        self._main_frame.rowconfigure(2, weight=0)
        self._main_frame.columnconfigure(0, weight=1)

        self.notebook = ttk.Notebook(self._main_frame)
        self.notebook.grid(row=0, column=0, sticky="nsew", padx=2, pady=(2, 0))
        self.setup_tab = ttk.Frame(self.notebook)
        self.measure_tab = ttk.Frame(self.notebook)
        self.notebook.add(self.setup_tab, text="  Parametrage  ")
        self.notebook.add(self.measure_tab, text="  Mesure  ")
        self.notebook.bind("<<NotebookTabChanged>>", lambda _e: self._update_measure_zone_label())

        self._build_setup_tab(self.setup_tab)
        self._build_measure_tab(self.measure_tab)

        self._log_frame = ttk.LabelFrame(self._main_frame, text="Journal", padding=4)
        self._log_frame.grid(row=1, column=0, sticky="ew", padx=2, pady=(2, 0))
        self.log_text = tk.Text(self._log_frame, height=5, font=("Consolas", 9))
        scroll = ttk.Scrollbar(self._log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.log_text.pack(fill="both", expand=True)

        self._statusbar = ttk.Frame(self._main_frame, relief="sunken", borderwidth=1)
        self._statusbar.grid(row=2, column=0, sticky="ew", padx=2, pady=(2, 2))
        ttk.Label(self._statusbar, textvariable=self.status_var, foreground="blue", width=42, anchor="w").pack(side="left", padx=6)
        ttk.Label(self._statusbar, textvariable=self.fps_var, width=14, anchor="w").pack(side="left", padx=6)
        ttk.Label(self._statusbar, textvariable=self.laser_coord_var, width=24, anchor="w").pack(side="left", padx=6)
        ttk.Button(self._statusbar, text="Live", width=8, command=self.on_start_live).pack(side="right", padx=2)
        ttk.Button(self._statusbar, text="Stop", width=8, command=self.on_stop_live).pack(side="right", padx=2)

    def _build_setup_tab(self, parent):
        parent.rowconfigure(0, weight=1)
        parent.columnconfigure(0, weight=0)
        parent.columnconfigure(1, weight=1)

        left = ttk.Frame(parent, padding=6)
        left.grid(row=0, column=0, sticky="ns")
        self._setup_params_frame = left
        self._build_pot_connection(left)
        self._build_pot_params(left)
        self._build_objective_laser_controls(left)

        right = ttk.Frame(parent)
        right.grid(row=0, column=1, sticky="nsew")
        right.rowconfigure(0, weight=1)
        right.rowconfigure(1, weight=0)
        right.columnconfigure(0, weight=1)

        self._preview_frame = ttk.Frame(right, relief="sunken", borderwidth=1)
        self._preview_frame.grid(row=0, column=0, sticky="nsew", padx=2, pady=(2, 0))
        self._preview_frame.rowconfigure(0, weight=1)
        self._preview_frame.columnconfigure(0, weight=1)
        self.preview_label = ttk.Label(self._preview_frame, text="Aucune image - Connecter la camera depuis le menu", anchor="center", background="#1a1a1a", foreground="#888888")
        self.preview_label.grid(row=0, column=0, sticky="nsew")
        self.preview_label.bind("<Configure>", self._on_preview_resize)
        self.preview_label.bind("<Button-1>", self._on_preview_click)

        self._controls_frame = ttk.Frame(right)
        self._controls_frame.grid(row=1, column=0, sticky="ew", padx=2, pady=(2, 0))
        self._build_controls()

    def _safe_focus_get(self):
        try:
            return self.focus_get()
        except Exception:
            return None

    def _defocus_entry(self, event=None):
        current_focus = self._safe_focus_get()
        if current_focus is None or not hasattr(current_focus, "winfo_class"):
            return
        if current_focus.winfo_class() in {"Entry", "TEntry", "Text", "Spinbox", "TSpinbox"}:
            self.focus_set()

    def _on_global_click(self, event):
        widget = getattr(event, "widget", None)
        wc = widget.winfo_class() if hasattr(widget, "winfo_class") else ""
        if wc not in {"Entry", "TEntry", "Text", "Spinbox", "TSpinbox", "Combobox", "TCombobox"}:
            current_focus = self._safe_focus_get()
            if current_focus is not None and hasattr(current_focus, "winfo_class"):
                if current_focus.winfo_class() in {"Entry", "TEntry", "Text", "Spinbox", "TSpinbox"}:
                    self.focus_set()

    def _current_objective_mm_per_px(self) -> float:
        name = self.objective_var.get().strip().lower()
        mag = 4.0
        if name.endswith("x"):
            try:
                mag = float(name[:-1])
            except Exception:
                mag = 4.0
        if mag <= 0:
            mag = 4.0
        return 3.45 / (mag * 1000.0)

    def _preview_to_frame_coords(self, click_x: int, click_y: int):
        if self._last_image is None:
            return None
        sw, sh = self._last_image.size
        tw = max(self.preview_label.winfo_width(), 1)
        th = max(self.preview_label.winfo_height(), 1)
        if sw <= 0 or sh <= 0:
            return None
        scale = min(tw / sw, th / sh)
        if scale <= 0:
            return None
        fw = max(int(sw * scale), 1)
        fh = max(int(sh * scale), 1)
        ox = max((tw - fw) // 2, 0)
        oy = max((th - fh) // 2, 0)
        local_x = click_x - ox
        local_y = click_y - oy
        if local_x < 0 or local_y < 0 or local_x >= fw or local_y >= fh:
            return None
        frame_x = int(round(local_x / scale))
        frame_y = int(round(local_y / scale))
        frame_x = max(0, min(sw - 1, frame_x))
        frame_y = max(0, min(sh - 1, frame_y))
        return frame_x, frame_y

    def _frame_point_to_motor_target(self, frame_x: int, frame_y: int, base_x: float, base_y: float):
        mm_per_px = self._current_objective_mm_per_px()
        dx_px = int(frame_x - int(self.laser_x))
        dy_px = int(frame_y - int(self.laser_y))
        target_x = base_x + dx_px * mm_per_px
        target_y = base_y + dy_px * mm_per_px
        if not (main_ui_mod.ABS_MIN_MM <= target_x <= main_ui_mod.ABS_MAX_MM):
            raise main_ui_mod.ConexError(f"Zone X hors limites ({target_x:.4f} mm)")
        if not (main_ui_mod.ABS_MIN_MM <= target_y <= main_ui_mod.ABS_MAX_MM):
            raise main_ui_mod.ConexError(f"Zone Y hors limites ({target_y:.4f} mm)")
        return target_x, target_y

    def _clear_sequence_preview_selection(self):
        self._seq_select_first_frame = None
        self._seq_rect_start_frame = None
        self._seq_rect_end_frame = None
        self._seq_select_base_motor = None
        if not self._seq_select_armed:
            self.seq_pick_status_var.set("Zone image : inactive")
        if self._last_image is not None:
            self._render_preview(self._last_image)

    def _set_seq_select_armed(self, armed: bool):
        self._seq_select_armed = bool(armed)
        self.seq_pick_btn_var.set("Annuler zone" if armed else "Zone image")

    def _update_sequence_labels(self, start_xy, end_xy):
        x0, y0 = start_xy
        x1, y1 = end_xy
        self.seq_start_motor = (x0, y0)
        self.seq_end_motor = (x1, y1)
        self.seq_start_lbl_var.set(f"Depart  : X={x0:.4f}  Y={y0:.4f} mm")
        self.seq_end_lbl_var.set(f"Arrivee : X={x1:.4f}  Y={y1:.4f} mm")
        self._update_measure_zone_label()

    def on_arm_seq_rectangle(self):
        if self._seq_select_armed:
            self._set_seq_select_armed(False)
            self._clear_sequence_preview_selection()
            self._log("Zone image annulee.")
            return
        if self.stream_thread is None or self._last_image is None:
            messagebox.showerror("Zone image", "Demarrer d'abord le flux live.")
            return
        try:
            base_x, base_y = self._read_motor_positions()
        except Exception as exc:
            messagebox.showerror("Zone image", str(exc))
            return
        self._seq_select_first_frame = None
        self._seq_rect_start_frame = None
        self._seq_rect_end_frame = None
        self._seq_select_base_motor = (base_x, base_y)
        self.seq_pick_status_var.set("Zone image : clic 1/2")
        self._set_seq_select_armed(True)
        self._log(f"Zone image armee depuis X={base_x:.4f} Y={base_y:.4f} mm.")
        if self._last_image is not None:
            self._render_preview(self._last_image)

    def _handle_sequence_preview_click(self, event):
        point = self._preview_to_frame_coords(event.x, event.y)
        if point is None:
            self._log("Zone image: clic en dehors de l'image")
            return
        if self._seq_select_base_motor is None:
            self._set_seq_select_armed(False)
            self.seq_pick_status_var.set("Zone image : inactive")
            return
        if self._seq_select_first_frame is None:
            self._seq_select_first_frame = point
            self._seq_rect_start_frame = point
            self._seq_rect_end_frame = point
            self.seq_pick_status_var.set("Zone image : clic 2/2")
            if self._last_image is not None:
                self._render_preview(self._last_image)
            return
        first_point = self._seq_select_first_frame
        base_x, base_y = self._seq_select_base_motor
        try:
            start_xy = self._frame_point_to_motor_target(first_point[0], first_point[1], base_x, base_y)
            end_xy = self._frame_point_to_motor_target(point[0], point[1], base_x, base_y)
        except Exception as exc:
            self._set_seq_select_armed(False)
            self._clear_sequence_preview_selection()
            messagebox.showerror("Zone image", str(exc))
            return
        self._seq_rect_start_frame = first_point
        self._seq_rect_end_frame = point
        self._seq_select_first_frame = None
        self._seq_select_base_motor = None
        self._update_sequence_labels(start_xy, end_xy)
        self.seq_mode_var.set("Rectangle")
        self.seq_pick_status_var.set("Zone image : zone definie")
        self._set_seq_select_armed(False)
        self._log(
            f"Zone image definie: ({start_xy[0]:.4f},{start_xy[1]:.4f}) -> ({end_xy[0]:.4f},{end_xy[1]:.4f}) mm"
        )
        if self._last_image is not None:
            self._render_preview(self._last_image)

    def _draw_sequence_overlay(self, image):
        start_pt = self._seq_rect_start_frame or self._seq_select_first_frame
        end_pt = self._seq_rect_end_frame
        if start_pt is None:
            return image
        if image.mode != "RGB":
            image = image.convert("RGB")
        draw = main_ui_mod.ImageDraw.Draw(image)
        sx, sy = start_pt
        if end_pt is None or end_pt == start_pt:
            r = 8
            draw.ellipse((sx - r, sy - r, sx + r, sy + r), outline=(0, 255, 128), width=2)
            return image
        ex, ey = end_pt
        x0, x1 = sorted((sx, ex))
        y0, y1 = sorted((sy, ey))
        draw.rectangle((x0, y0, x1, y1), outline=(0, 255, 128), width=2)
        for px, py in ((sx, sy), (ex, ey)):
            draw.ellipse((px - 4, py - 4, px + 4, py + 4), fill=(0, 255, 128), outline=(0, 90, 40), width=1)
        return image

    def _render_preview(self, image):
        display = self._draw_laser_overlay(image.copy())
        display = self._draw_sequence_overlay(display)
        fitted = self._fit_image_to_preview(display)
        self.preview_photo = main_ui_mod.ImageTk.PhotoImage(image=fitted)
        self.preview_label.configure(image=self.preview_photo, text="")

    def _on_preview_click(self, event):
        if self._seq_select_armed:
            self._handle_sequence_preview_click(event)
        return None

    def _build_objective_laser_controls(self, parent):
        obj_box = ttk.LabelFrame(parent, text="Objectif / Laser", padding=4)
        obj_box.pack(fill="x", pady=(0, 6))

        ttk.Label(obj_box, text="Objectif").grid(row=0, column=0, sticky="w", padx=2)
        obj_combo = ttk.Combobox(
            obj_box,
            textvariable=self.objective_var,
            values=list(self.OBJECTIVE_PRESETS.keys()),
            state="readonly",
            width=5,
        )
        obj_combo.grid(row=0, column=1, padx=2)
        obj_combo.bind("<<ComboboxSelected>>", lambda _e: self.on_objective_change())
        ttk.Button(obj_box, text="Appliquer", command=self.on_objective_change, width=8).grid(row=0, column=2, padx=4)

        ttk.Label(obj_box, text="Pas (px)").grid(row=1, column=0, sticky="w", padx=2)
        ttk.Entry(obj_box, textvariable=self.laser_step_var, width=5).grid(row=1, column=1, padx=2)
        ttk.Button(obj_box, text="X-", width=3, command=lambda: self.on_laser_move(-1, 0)).grid(row=1, column=2, padx=1)
        ttk.Button(obj_box, text="X+", width=3, command=lambda: self.on_laser_move(+1, 0)).grid(row=1, column=3, padx=1)
        ttk.Button(obj_box, text="Y-", width=3, command=lambda: self.on_laser_move(0, -1)).grid(row=1, column=4, padx=1)
        ttk.Button(obj_box, text="Y+", width=3, command=lambda: self.on_laser_move(0, +1)).grid(row=1, column=5, padx=1)

        ttk.Label(obj_box, text="Taille (px)").grid(row=2, column=0, sticky="w", padx=2)
        ttk.Entry(obj_box, textvariable=self.laser_size_var, width=5).grid(row=2, column=1, padx=2)
        ttk.Button(obj_box, text="-", width=3, command=lambda: self.on_laser_size(-1)).grid(row=2, column=2, padx=1)
        ttk.Button(obj_box, text="+", width=3, command=lambda: self.on_laser_size(+1)).grid(row=2, column=3, padx=1)

    def _build_controls(self):
        outer = self._controls_frame

        motor_box = ttk.LabelFrame(outer, text="Moteurs", padding=4)
        motor_box.pack(side="left", fill="y", padx=(0, 4))

        ttk.Label(motor_box, text="Pas X (mm)").grid(row=0, column=0, sticky="w", padx=2)
        ttk.Entry(motor_box, textvariable=self.step_x_var, width=7).grid(row=0, column=1, padx=2)
        ttk.Button(motor_box, text="X -", width=4, command=lambda: self.on_motor_jog("X", -1, False)).grid(row=0, column=2, padx=1)
        ttk.Button(motor_box, text="X +", width=4, command=lambda: self.on_motor_jog("X", +1, False)).grid(row=0, column=3, padx=1)

        ttk.Label(motor_box, text="Pas Y (mm)").grid(row=1, column=0, sticky="w", padx=2)
        ttk.Entry(motor_box, textvariable=self.step_y_var, width=7).grid(row=1, column=1, padx=2)
        ttk.Button(motor_box, text="Y -", width=4, command=lambda: self.on_motor_jog("Y", -1, False)).grid(row=1, column=2, padx=1)
        ttk.Button(motor_box, text="Y +", width=4, command=lambda: self.on_motor_jog("Y", +1, False)).grid(row=1, column=3, padx=1)

        ttk.Label(motor_box, text="Plage : 0 - 25 mm", foreground="gray", font=("Segoe UI", 8)).grid(row=2, column=0, columnspan=4, sticky="w", pady=(1, 0))
        ttk.Label(motor_box, text="Vit. clavier (mm/s)").grid(row=3, column=0, columnspan=2, sticky="w", padx=2, pady=(4, 0))
        ttk.Entry(motor_box, textvariable=self.jog_speed_var, width=7).grid(row=3, column=2, columnspan=2, sticky="w", padx=2, pady=(4, 0))
        ttk.Label(motor_box, text="Clavier continu : Left/Right/Up/Down | Echap = quitter champ", foreground="gray", font=("Segoe UI", 8)).grid(row=4, column=0, columnspan=4, sticky="w", pady=(2, 0))

        seq_box = ttk.LabelFrame(outer, text="Sequence balayage", padding=4)
        seq_box.pack(side="left", fill="y", padx=(0, 4))

        ttk.Label(seq_box, textvariable=self.seq_start_lbl_var, width=24, foreground="#006600").grid(row=0, column=0, columnspan=4, sticky="w", padx=2)
        ttk.Label(seq_box, textvariable=self.seq_end_lbl_var, width=24, foreground="#880000").grid(row=1, column=0, columnspan=4, sticky="w", padx=2)

        ttk.Label(seq_box, text="Mode").grid(row=2, column=0, sticky="w", padx=2)
        ttk.Combobox(seq_box, textvariable=self.seq_mode_var, values=[self.seq_mode_var.get(), "Rectangle"], state="readonly", width=9).grid(row=2, column=1, padx=2)
        ttk.Label(seq_box, text="Pas (mm)").grid(row=2, column=2, sticky="w", padx=(8, 2))
        ttk.Entry(seq_box, textvariable=self.seq_step_mm_var, width=7).grid(row=2, column=3, padx=2)

        ttk.Label(seq_box, text="Duree/pt (s)").grid(row=3, column=0, sticky="w", padx=2)
        ttk.Entry(seq_box, textvariable=self.seq_duration_var, width=5).grid(row=3, column=1, padx=2)

        ttk.Button(seq_box, text="Set Depart", command=self.on_set_seq_start).grid(row=4, column=0, padx=2, pady=(4, 0))
        ttk.Button(seq_box, text="Set Arrivee", command=self.on_set_seq_end).grid(row=4, column=1, padx=2, pady=(4, 0))
        self._seq_run_btn = ttk.Button(seq_box, text="Lancer", command=self.on_run_sequence)
        self._seq_run_btn.grid(row=4, column=2, padx=2, pady=(4, 0))
        ttk.Button(seq_box, text="Stop", command=self.on_stop_sequence).grid(row=4, column=3, padx=2, pady=(4, 0))

        ttk.Button(seq_box, textvariable=self.seq_pick_btn_var, command=self.on_arm_seq_rectangle).grid(row=5, column=0, columnspan=2, padx=2, pady=(4, 0), sticky="w")
        ttk.Label(seq_box, textvariable=self.seq_pick_status_var, foreground="gray", font=("Segoe UI", 8)).grid(row=5, column=2, columnspan=2, sticky="w", pady=(4, 0))
        ttk.Label(seq_box, textvariable=self.seq_status_var, foreground="blue", font=("Segoe UI", 8)).grid(row=6, column=0, columnspan=4, sticky="w", pady=(2, 0))

        cam_box = ttk.LabelFrame(outer, text="Camera rapide", padding=4)
        cam_box.pack(side="left", fill="y", padx=(0, 4))

        ttk.Label(cam_box, text="Expo (s)").grid(row=0, column=0, sticky="w", padx=2)
        ttk.Entry(cam_box, textvariable=self.exposure_var, width=9).grid(row=0, column=1, padx=2)
        ttk.Label(cam_box, text="Gain").grid(row=0, column=2, sticky="w", padx=(8, 2))
        ttk.Entry(cam_box, textvariable=self.gain_var, width=6).grid(row=0, column=3, padx=2)
        ttk.Button(cam_box, text="Appliquer", command=self.on_apply_camera_params).grid(row=0, column=4, padx=4)

        ttk.Label(cam_box, textvariable=self.gain_info_var, foreground="gray", font=("Segoe UI", 8)).grid(row=1, column=0, columnspan=5, sticky="w", pady=(2, 0))

    def _build_measure_tab(self, parent):
        parent.rowconfigure(1, weight=1)
        parent.columnconfigure(0, weight=1)

        top = ttk.LabelFrame(parent, text="Mesure", padding=6)
        top.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 4))
        top.columnconfigure(4, weight=1)
        ttk.Button(top, text="Demarrer mesure spatiale", command=self._on_start_spatial_measure).grid(row=0, column=0, padx=2)
        self.btn_pot_stop = ttk.Button(top, text="Stop", command=self._on_stop_pot_measurement, state="disabled")
        self.btn_pot_stop.grid(row=0, column=1, padx=2)
        ttk.Button(top, text="Exporter CSV", command=self._on_export_pot_csv).grid(row=0, column=2, padx=2)
        ttk.Button(top, text="Exporter matrice", command=self._on_export_matrix_csv).grid(row=0, column=3, padx=2)
        ttk.Label(top, textvariable=self.pot_zone_var, foreground="#444444").grid(row=0, column=4, sticky="w", padx=(12, 4))
        ttk.Label(top, textvariable=self.pot_progress_var, foreground="#006600").grid(row=0, column=5, sticky="e", padx=4)
        ttk.Label(top, textvariable=self.pot_count_var).grid(row=0, column=6, sticky="e", padx=4)

        body = ttk.PanedWindow(parent, orient="vertical")
        body.grid(row=1, column=0, sticky="nsew", padx=6, pady=(0, 6))
        graph_wrap = ttk.Frame(body)
        body.add(graph_wrap, weight=1)
        graph_top = ttk.Frame(graph_wrap)
        graph_top.pack(fill="x")
        for gt in ["I = f(t)", "Ewe = f(t)", "I = f(Ewe)", "Ewe = f(I)"]:
            ttk.Radiobutton(graph_top, text=gt, value=gt, variable=self.pot_graph_type_var, command=self._update_plot).pack(side="left", padx=3)
        self.graph_canvas = tk.Canvas(graph_wrap, bg="white", highlightthickness=0, height=240)
        self.graph_canvas.pack(fill="both", expand=True)
        self.graph_canvas.bind("<Configure>", lambda _e: self._update_plot())
        matrix_wrap = ttk.Frame(body)
        body.add(matrix_wrap, weight=2)
        self.matrix_canvas = tk.Canvas(matrix_wrap, bg="#f0f0f0", highlightthickness=0)
        self.matrix_canvas.pack(fill="both", expand=True)
        self.matrix_canvas.bind("<Configure>", lambda _e: self._update_matrix())

    def _build_pot_connection(self, parent):
        box = ttk.LabelFrame(parent, text="Potentiostat", padding=6)
        box.pack(fill="x", pady=(0, 6))
        ttk.Label(box, text="Chemin DLL").grid(row=0, column=0, sticky="w")
        ttk.Entry(box, textvariable=self.pot_dll_path_var, width=38).grid(row=0, column=1, columnspan=3, sticky="ew", padx=4)
        ttk.Button(box, text="Parcourir", command=self._browse_pot_dll).grid(row=0, column=4, padx=4)
        ttk.Label(box, text="IP").grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Entry(box, textvariable=self.pot_address_var, width=18).grid(row=1, column=1, sticky="w", padx=4, pady=(6, 0))
        ttk.Label(box, text="Canal").grid(row=1, column=2, sticky="e", pady=(6, 0))
        ttk.Spinbox(box, from_=1, to=16, textvariable=self.pot_channel_var, width=4).grid(row=1, column=3, sticky="w", padx=4, pady=(6, 0))
        ttk.Button(box, text="Connecter", command=self._on_pot_connect).grid(row=2, column=1, sticky="ew", padx=2, pady=(6, 0))
        ttk.Button(box, text="Firmware", command=self._on_pot_load_firmware).grid(row=2, column=2, sticky="ew", padx=2, pady=(6, 0))
        ttk.Button(box, text="Deconnecter", command=self._on_pot_disconnect).grid(row=2, column=3, sticky="ew", padx=2, pady=(6, 0))
        ttk.Label(box, textvariable=self.pot_status_var, foreground="blue").grid(row=3, column=0, columnspan=5, sticky="w", pady=(6, 0))

    def _build_pot_params(self, parent):
        box = ttk.LabelFrame(parent, text="Parametres CA", padding=6)
        box.pack(fill="x", pady=(0, 6))
        ttk.Label(box, text="Ewe (V)").grid(row=0, column=0, sticky="e")
        ttk.Entry(box, textvariable=self.pot_voltage_var, width=10).grid(row=0, column=1, padx=4, sticky="w")
        ttk.Label(box, text="vs").grid(row=0, column=2, sticky="e")
        ttk.Combobox(box, textvariable=self.pot_vs_var, values=ps.VS_LABELS, state="readonly", width=6).grid(row=0, column=3, padx=4, sticky="w")
        ttk.Label(box, text="Duree CA (s)").grid(row=1, column=0, sticky="e", pady=(4, 0))
        ttk.Entry(box, textvariable=self.pot_duration_var, width=10).grid(row=1, column=1, padx=4, sticky="w", pady=(4, 0))
        ttk.Label(box, text="Record dta").grid(row=2, column=0, sticky="e", pady=(4, 0))
        ttk.Entry(box, textvariable=self.pot_record_dt_var, width=10).grid(row=2, column=1, padx=4, sticky="w", pady=(4, 0))
        ttk.Label(box, text="E Range").grid(row=3, column=0, sticky="e", pady=(4, 0))
        ttk.Combobox(box, textvariable=self.pot_e_range_var, values=[label for label, _ in ps.E_RANGE_LABELS], state="readonly", width=16).grid(row=3, column=1, columnspan=3, padx=4, sticky="w", pady=(4, 0))
        ttk.Label(box, text="I Range").grid(row=4, column=0, sticky="e", pady=(4, 0))
        ttk.Combobox(box, textvariable=self.pot_i_range_var, values=[label for label, _ in ps.I_RANGE_LABELS], state="readonly", width=16).grid(row=4, column=1, columnspan=3, padx=4, sticky="w", pady=(4, 0))
        ttk.Label(box, text="Bandwidth").grid(row=5, column=0, sticky="e", pady=(4, 0))
        ttk.Combobox(box, textvariable=self.pot_bw_var, values=[label for label, _ in ps.BW_LABELS], state="readonly", width=16).grid(row=5, column=1, columnspan=3, padx=4, sticky="w", pady=(4, 0))
        ttk.Label(box, text="Cycles").grid(row=6, column=0, sticky="e", pady=(4, 0))
        ttk.Entry(box, textvariable=self.pot_cycles_var, width=10).grid(row=6, column=1, padx=4, sticky="w", pady=(4, 0))

    def _toggle_log(self):
        if self._log_visible.get():
            self._log_frame.grid(row=1, column=0, sticky="ew", padx=2, pady=(2, 0))
        else:
            self._log_frame.grid_remove()

    def _toggle_controls(self):
        if self._controls_visible.get():
            self._controls_frame.grid(row=1, column=0, sticky="ew", padx=2, pady=(2, 0))
        else:
            self._controls_frame.grid_remove()

    def _browse_pot_dll(self):
        directory = filedialog.askdirectory(title="Selectionner le dossier contenant EClib64.dll")
        if directory:
            self.pot_dll_path_var.set(directory)

    def _ensure_pot_api(self):
        if self.pot_api is not None:
            return
        ps.import_kbio()
        ps.rebuild_ca_parms()
        dll_name = "EClib64.dll" if ps.c_is_64b else "EClib.dll"
        dll_path = os.path.join(self.pot_dll_path_var.get().strip(), dll_name)
        if not os.path.isfile(dll_path):
            raise FileNotFoundError(f"DLL introuvable : {dll_path}")
        self.pot_api = ps.KBIO_api(dll_path)
        self._log(f"EClib chargee - version {self.pot_api.GetLibVersion()}")

    def _on_pot_connect(self):
        try:
            self._ensure_pot_api()
            self.pot_connection_id, info = self.pot_api.Connect(self.pot_address_var.get().strip(), timeout=10)
            self.pot_board_type = self.pot_api.GetChannelBoardType(self.pot_connection_id, self.pot_channel_var.get())
            model = ps.KBIO.DEVICE(info.DeviceCode).name
            self.pot_status_var.set(f"Connecte - {model}")
            self._log(f"Potentiostat connecte : {model}, board_type={self.pot_board_type}")
        except Exception as exc:
            messagebox.showerror("Potentiostat", str(exc))

    def _on_pot_load_firmware(self):
        try:
            if self.pot_connection_id is None:
                raise RuntimeError("Potentiostat non connecte")
            dll_dir = self.pot_dll_path_var.get().strip()
            firmware, fpga = ps.select_firmware(self.pot_board_type)
            firmware_path = ps.resolve_resource_path(firmware, dll_dir)
            fpga_path = ps.resolve_resource_path(fpga, dll_dir) if fpga else ""
            self.pot_api.LoadFirmware(
                self.pot_connection_id,
                self.pot_api.channel_map({self.pot_channel_var.get()}),
                firmware=firmware_path,
                fpga=fpga_path,
                force=True,
            )
            self._log(f"Firmware charge : {firmware_path}")
            self.pot_status_var.set("Firmware charge")
        except Exception as exc:
            messagebox.showerror("Firmware", str(exc))

    def _on_pot_disconnect(self):
        self._on_stop_pot_measurement()
        if self.pot_connection_id is not None:
            try:
                self.pot_api.Disconnect(self.pot_connection_id)
            except Exception:
                pass
        self.pot_connection_id = None
        self.pot_board_type = None
        self.pot_status_var.set("Deconnecte")
        self._log("Potentiostat deconnecte")

    def _read_pot_params(self):
        duration_raw = self.pot_duration_var.get().strip()
        duration = float(duration_raw) if duration_raw else None
        if duration is not None and duration <= 0:
            raise ValueError("La duree CA doit etre > 0")
        step = ps.VoltageStep(
            voltage=float(self.pot_voltage_var.get()),
            duration=duration if duration is not None else 0.0,
            vs_init=(self.pot_vs_var.get() == "Pref"),
        )
        record_dt_raw = self.pot_record_dt_var.get().strip()
        record_dt = float(record_dt_raw) if record_dt_raw else None
        return {
            "steps": [step],
            "record_dt": record_dt,
            "n_cycles": int(self.pot_cycles_var.get()),
            "i_range": ps.resolve_label_value(self.pot_i_range_var.get(), ps.I_RANGE_LABELS),
            "e_range": ps.resolve_label_value(self.pot_e_range_var.get(), ps.E_RANGE_LABELS),
            "bw": ps.resolve_label_value(self.pot_bw_var.get(), ps.BW_LABELS),
        }

    def _build_ecc_params(self, cfg):
        step = cfg["steps"][0]
        record_dt = cfg.get("record_dt")
        if record_dt is None or record_dt <= 0:
            record_dt = max(60.0, float(step.duration))
        return ps.make_ecc_parms(
            self.pot_api,
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["voltage_step"], step.voltage, 0),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["step_duration"], step.duration, 0),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["vs_init"], step.vs_init, 0),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["nb_steps"], 0),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["record_dt"], record_dt),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["I_range"], cfg["i_range"]),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["E_range"], cfg["e_range"]),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["bandwidth"], cfg["bw"]),
            ps.make_ecc_parm(self.pot_api, ps.CA_PARMS["repeat"], cfg["n_cycles"]),
        )

    def _read_spatial_scan(self):
        if self.seq_start_motor is None or self.seq_end_motor is None:
            raise RuntimeError("Definir la zone avec les moteurs avant la mesure")
        step_mm = float(self.seq_step_mm_var.get())
        dwell_s = float(self.seq_duration_var.get())
        if step_mm <= 0 or dwell_s <= 0:
            raise ValueError("Pas (mm) et Duree/pt (s) doivent etre > 0")
        x0, y0 = self.seq_start_motor
        x1, y1 = self.seq_end_motor
        if self.seq_mode_var.get() == "Rectangle":
            waypoints = self._build_waypoints_rect(x0, y0, x1, y1, step_mm)
            rows = max(1, round(abs(y1 - y0) / step_mm)) + 1
            cols = max(1, round(abs(x1 - x0) / step_mm)) + 1
            order = [(r, c) for r in range(rows) for c in (range(cols) if r % 2 == 0 else range(cols - 1, -1, -1))]
        else:
            waypoints = self._build_waypoints_linear(x0, y0, x1, y1, step_mm)
            rows, cols = 1, len(waypoints)
            order = [(0, c) for c in range(cols)]
        return waypoints, rows, cols, order, dwell_s, self._get_motor_timeout()

    def _prepare_spatial_pot_config(self, cfg, point_count: int, dwell_s: float, timeout: float):
        step = cfg["steps"][0]
        transition_budget_s = max(0, point_count - 1) * max(2.0 * timeout, 0.0)
        dwell_budget_s = point_count * max(dwell_s, 0.0)
        required_duration_s = dwell_budget_s + transition_budget_s + 5.0
        if step.duration <= 0 or step.duration < required_duration_s:
            cfg = dict(cfg)
            cfg["steps"] = [ps.VoltageStep(voltage=step.voltage, duration=required_duration_s, vs_init=step.vs_init)]
            if step.duration > 0:
                self._log(
                    f"Duree CA auto-ajustee pour le scan : {required_duration_s:.1f} s "
                    f"(au lieu de {step.duration:.1f} s)"
                )
            else:
                self._log(f"Duree CA auto-definie pour le scan : {required_duration_s:.1f} s")
        return cfg

    def _on_start_spatial_measure(self):
        if self.pot_connection_id is None:
            messagebox.showwarning("Potentiostat", "Connectez le potentiostat d'abord")
            return
        if self._pot_thread is not None and self._pot_thread.is_alive():
            messagebox.showinfo("Mesure", "Une mesure est deja en cours")
            return
        try:
            cfg = self._read_pot_params()
            waypoints, rows, cols, order, dwell_s, timeout = self._read_spatial_scan()
            self._axis_or_error("X")
            self._axis_or_error("Y")
            cfg = self._prepare_spatial_pot_config(cfg, len(waypoints), dwell_s, timeout)
        except Exception as exc:
            messagebox.showerror("Mesure spatiale", str(exc))
            return
        self._pot_data_rows.clear()
        self._pot_plot_t.clear()
        self._pot_plot_I.clear()
        self._pot_plot_E.clear()
        self._pot_matrix = [[None] * cols for _ in range(rows)]
        self._pot_rows = rows
        self._pot_cols = cols
        self._pot_index = 0
        self._pot_running = True
        self._pot_stop_event.clear()
        self.btn_pot_stop.configure(state="normal")
        self.notebook.select(self.measure_tab)
        self._pot_thread = threading.Thread(target=self._run_spatial_measure, args=(cfg, waypoints, order, dwell_s, timeout), daemon=True)
        self._pot_thread.start()

    def _run_spatial_measure(self, cfg, waypoints, order, dwell_s, timeout):
        channel = None

        def sample_current_values() -> tuple[str, dict]:
            current_values = self.pot_api.GetCurrentValues(self.pot_connection_id, channel)
            state = getattr(current_values, "State", 0)
            status = "STOP" if state == 0 else "RUN"
            record = {
                "t": float(getattr(current_values, "ElapsedTime", 0.0)),
                "Ewe": float(getattr(current_values, "Ewe", 0.0)),
                "I": float(getattr(current_values, "I", 0.0)),
                "cycle": 0,
            }
            return status, record

        try:
            ax = self._axis_or_error("X")
            ay = self._axis_or_error("Y")
            start_x, start_y = waypoints[0]
            ax.move_absolute_no_wait(start_x)
            ay.move_absolute_no_wait(start_y)
            ax.wait_done(timeout_s=timeout)
            ay.wait_done(timeout_s=timeout)
            tech = ps.resolve_resource_path(ps.select_ca_tech_file(self.pot_board_type), self.pot_dll_path_var.get().strip())
            self.pot_api.LoadTechnique(self.pot_connection_id, self.pot_channel_var.get(), tech, self._build_ecc_params(cfg), first=True, last=True, display=False)
            self.pot_api.StartChannel(self.pot_connection_id, self.pot_channel_var.get())
            channel = self.pot_channel_var.get()
            total = len(waypoints)
            for idx, (wx, wy) in enumerate(waypoints):
                if self._pot_stop_event.is_set():
                    break
                if idx > 0:
                    ax.move_absolute_no_wait(wx)
                    ay.move_absolute_no_wait(wy)
                    ax.wait_done(timeout_s=timeout)
                    ay.wait_done(timeout_s=timeout)
                end_time = time.monotonic() + dwell_s
                while time.monotonic() < end_time and not self._pot_stop_event.is_set():
                    remaining = end_time - time.monotonic()
                    if remaining <= 0:
                        break
                    time.sleep(min(0.02, remaining))
                if self._pot_stop_event.is_set():
                    break
                status, point = sample_current_values()
                self._pot_data_rows.append(point)
                self._pot_plot_t.append(point["t"])
                self._pot_plot_I.append(point["I"])
                self._pot_plot_E.append(point["Ewe"])
                row, col = order[idx]
                self._pot_matrix[row][col] = point["I"]
                self._pot_index = idx
                self.after(0, lambda i=idx + 1, t=total: self.pot_progress_var.set(f"Mesure {i}/{t}"))
                self.after(0, self._update_plot)
                self.after(0, self._update_matrix)
                if status == "STOP":
                    self._pot_stop_event.set()
                    break
        except Exception as exc:
            self.after(0, lambda e=str(exc): self._log(f"Erreur mesure spatiale : {e}"))
        finally:
            if channel is not None:
                try:
                    self.pot_api.StopChannel(self.pot_connection_id, channel)
                except Exception:
                    pass
            self._pot_running = False
            self.after(0, self._finish_pot_measure)

    def _parse_ca_records(self, data):
        current_values, data_info, data_record = data
        ix = 0
        for _ in range(data_info.NbRows):
            inx = ix + data_info.NbCols
            if inx > len(data_record):
                break
            t_high, t_low, *row = data_record[ix:inx]
            if len(row) >= 2:
                yield {
                    "t": current_values.TimeBase * ((t_high << 32) + t_low),
                    "Ewe": self.pot_api.ConvertChannelNumericIntoSingle(row[0], self.pot_board_type),
                    "I": self.pot_api.ConvertChannelNumericIntoSingle(row[1], self.pot_board_type),
                    "cycle": row[2] if len(row) >= 3 else 0,
                }
            ix = inx

    def _finish_pot_measure(self):
        self.btn_pot_stop.configure(state="disabled")
        self.pot_count_var.set(f"Points : {len(self._pot_data_rows)}")
        self.pot_progress_var.set("Termine")
        self._update_plot()
        self._update_matrix()

    def _on_stop_pot_measurement(self):
        self._pot_stop_event.set()

    def _update_plot(self):
        self.pot_count_var.set(f"Points : {len(self._pot_plot_t)}")
        self.graph_canvas.delete("all")
        if len(self._pot_plot_t) < 2:
            return
        width = self.graph_canvas.winfo_width()
        height = self.graph_canvas.winfo_height()
        if width < 50 or height < 50:
            return
        kind = self.pot_graph_type_var.get()
        if kind == "I = f(t)":
            xs, ys = self._pot_plot_t, self._pot_plot_I
            title = "I = f(t)"
        elif kind == "Ewe = f(t)":
            xs, ys = self._pot_plot_t, self._pot_plot_E
            title = "Ewe = f(t)"
        elif kind == "I = f(Ewe)":
            xs, ys = self._pot_plot_E, self._pot_plot_I
            title = "I = f(Ewe)"
        else:
            xs, ys = self._pot_plot_I, self._pot_plot_E
            title = "Ewe = f(I)"
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        if x_max == x_min:
            x_max += 1
        if y_max == y_min:
            y_max += 1
        left, right, top, bottom = 60, 20, 25, 35
        pw = width - left - right
        ph = height - top - bottom
        self.graph_canvas.create_rectangle(left, top, width - right, height - bottom, fill="#fafafa", outline="")
        self.graph_canvas.create_text(width // 2, 10, text=title, font=("", 10, "bold"))
        for xv, yv in zip(xs, ys):
            cx = left + (xv - x_min) / (x_max - x_min) * pw
            cy = top + (1 - (yv - y_min) / (y_max - y_min)) * ph
            self.graph_canvas.create_line(cx - 2, cy - 2, cx + 2, cy + 2, fill="#1f77b4")
            self.graph_canvas.create_line(cx - 2, cy + 2, cx + 2, cy - 2, fill="#1f77b4")

    def _update_matrix(self):
        self.matrix_canvas.delete("all")
        if self._pot_rows <= 0 or self._pot_cols <= 0:
            return
        width = self.matrix_canvas.winfo_width()
        height = self.matrix_canvas.winfo_height()
        if width < 50 or height < 50:
            return
        cell_w = max((width - 60) / self._pot_cols, 1)
        cell_h = max((height - 40) / self._pot_rows, 1)
        values = [v for row in self._pot_matrix for v in row if v is not None]
        v_min = min(values) if values else 0.0
        v_max = max(values) if values else 1.0
        if v_max == v_min:
            v_max = v_min + 1.0
        for r in range(self._pot_rows):
            for c in range(self._pot_cols):
                x0 = 50 + c * cell_w
                y0 = 20 + r * cell_h
                x1 = x0 + cell_w
                y1 = y0 + cell_h
                value = self._pot_matrix[r][c]
                color = "#e0e0e0" if value is None else ps.value_to_color(value, v_min, v_max)
                self.matrix_canvas.create_rectangle(x0, y0, x1, y1, fill=color, outline="#999999")
                if value is not None:
                    self.matrix_canvas.create_text((x0 + x1) / 2, (y0 + y1) / 2, text=ps.format_current(value), font=("", 8))

    def _on_export_pot_csv(self):
        if not self._pot_data_rows:
            messagebox.showinfo("CSV", "Aucune donnee a exporter")
            return
        path = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")], initialfile="potentiostat_data.csv")
        if not path:
            return
        with open(path, "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["t", "Ewe", "I", "cycle"])
            writer.writeheader()
            writer.writerows(self._pot_data_rows)

    def _on_export_matrix_csv(self):
        if not any(v is not None for row in self._pot_matrix for v in row):
            messagebox.showinfo("Matrice", "Aucune matrice a exporter")
            return
        path = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")], initialfile="spatial_map.csv")
        if not path:
            return
        with open(path, "w", newline="") as handle:
            writer = csv.writer(handle)
            for row in self._pot_matrix:
                writer.writerow(["" if value is None else f"{value:.6e}" for value in row])

    def _update_measure_zone_label(self):
        if self.seq_start_motor is None or self.seq_end_motor is None:
            self.pot_zone_var.set("Zone : non definie")
            return
        x0, y0 = self.seq_start_motor
        x1, y1 = self.seq_end_motor
        try:
            step_mm = float(self.seq_step_mm_var.get())
            cols = max(1, round(abs(x1 - x0) / step_mm)) + 1
            rows = max(1, round(abs(y1 - y0) / step_mm)) + 1 if self.seq_mode_var.get() == "Rectangle" else 1
            self.pot_zone_var.set(f"Zone : {self.seq_mode_var.get()} | grille {rows}x{cols}")
        except Exception:
            self.pot_zone_var.set(f"Zone : {self.seq_mode_var.get()} | X={x0:.3f}->{x1:.3f}, Y={y0:.3f}->{y1:.3f}")

    def on_set_seq_start(self):
        super().on_set_seq_start()
        self.after(200, self._update_measure_zone_label)

    def on_set_seq_end(self):
        super().on_set_seq_end()
        self.after(200, self._update_measure_zone_label)

    def _on_close(self):
        self._on_stop_pot_measurement()
        if self._pot_thread is not None and self._pot_thread.is_alive():
            self._pot_thread.join(timeout=3.0)
        if self.pot_connection_id is not None and self.pot_api is not None:
            try:
                self.pot_api.Disconnect(self.pot_connection_id)
            except Exception:
                pass
        super()._on_close()


def main():
    app = IntegratedMeasurementApp()
    app.mainloop()


if __name__ == "__main__":
    main()


