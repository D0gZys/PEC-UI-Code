import os
import queue
import sys
import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import messagebox, ttk

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageTk
except Exception as dep_exc:  # noqa: BLE001
    print("Missing Python dependencies.")
    print("Install with:")
    print("  python -m pip install numpy pillow")
    raise SystemExit(dep_exc) from dep_exc

from newport_conex_test_ui import ConexAxis, ConexError, DEFAULT_DLL_CANDIDATES, load_conex_class


class CameraStreamThread(threading.Thread):
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
            height, width = frame.image_buffer.shape[:2]
            rgb = self.mono_to_color_processor.transform_to_24(frame.image_buffer, width, height)
            rgb = rgb.reshape(height, width, 3)
            return Image.fromarray(rgb, mode="RGB")
        shift = max(self.bit_depth - 8, 0)
        gray8 = (frame.image_buffer >> shift).astype(np.uint8)
        return Image.fromarray(gray8, mode="L")

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


class MotorCameraComboApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Motor + Camera Test UI")
        self.geometry("1600x960")
        self.minsize(1300, 820)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.project_root = Path(__file__).resolve().parent.parent

        self.conex_class = None
        self.loaded_motor_dll = ""
        self.axis_x = None
        self.axis_y = None

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

        self.TLCameraSDK = None
        self.SENSOR_TYPE = None
        self.MonoToColorProcessorSDK = None

        default_motor_dll = ""
        for candidate in DEFAULT_DLL_CANDIDATES:
            if Path(candidate).exists():
                default_motor_dll = candidate
                break

        self.motor_dll_var = tk.StringVar(value=default_motor_dll)
        self.x_port_var = tk.StringVar()
        self.y_port_var = tk.StringVar()
        self.motor_addr_var = tk.StringVar(value="1")
        self.motor_timeout_var = tk.StringVar(value="30")
        self.step_x_var = tk.StringVar(value="0.05")
        self.step_y_var = tk.StringVar(value="0.05")

        self.serial_var = tk.StringVar(value="")
        self.exposure_var = tk.StringVar(value="10000")
        self.gain_var = tk.StringVar(value="0")
        self.gain_info_var = tk.StringVar(value="Gain: unknown")
        self.camera_dll_path_var = tk.StringVar(value=str(self._native_dll_dir()))

        self.status_var = tk.StringVar(value="Ready")
        self.fps_var = tk.StringVar(value="FPS: -")
        self.laser_x = 0
        self.laser_y = 0
        self.laser_initialized = False
        self.laser_coord_var = tk.StringVar(value="Laser: X=- Y=-")
        self.laser_step_var = tk.StringVar(value="5")
        self.laser_size_var = tk.StringVar(value="6")

        self._build_ui()
        self._bind_keyboard_controls()
        self._log("Ready.")

    def _build_ui(self):
        root = ttk.Frame(self, padding=10)
        root.pack(fill="both", expand=True)

        motors = ttk.LabelFrame(root, text="Motors (Newport CONEX-CC)", padding=10)
        motors.pack(fill="x")

        ttk.Label(motors, text="DLL path").grid(row=0, column=0, sticky="w")
        ttk.Entry(motors, textvariable=self.motor_dll_var, width=80).grid(
            row=0, column=1, columnspan=5, sticky="ew", padx=6
        )
        ttk.Button(motors, text="Load Motor DLL", command=self.on_load_motor_dll).grid(row=0, column=6, padx=6)
        ttk.Button(motors, text="Scan Motors", command=self.on_scan_motors).grid(row=0, column=7, padx=6)

        ttk.Label(motors, text="X COM").grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.x_combo = ttk.Combobox(motors, textvariable=self.x_port_var, width=20, state="readonly")
        self.x_combo.grid(row=1, column=1, sticky="w", pady=(8, 0))
        ttk.Label(motors, text="Y COM").grid(row=1, column=2, sticky="w", pady=(8, 0))
        self.y_combo = ttk.Combobox(motors, textvariable=self.y_port_var, width=20, state="readonly")
        self.y_combo.grid(row=1, column=3, sticky="w", pady=(8, 0))
        ttk.Label(motors, text="Address").grid(row=1, column=4, sticky="e", pady=(8, 0))
        ttk.Entry(motors, textvariable=self.motor_addr_var, width=6).grid(row=1, column=5, sticky="w", pady=(8, 0))
        ttk.Label(motors, text="Timeout (s)").grid(row=1, column=6, sticky="e", pady=(8, 0))
        ttk.Entry(motors, textvariable=self.motor_timeout_var, width=8).grid(row=1, column=7, sticky="w", pady=(8, 0))

        ttk.Button(motors, text="Connect Motors", command=self.on_connect_motors).grid(
            row=2, column=1, sticky="w", pady=(10, 0)
        )
        ttk.Button(motors, text="Initialize (Home X+Y)", command=self.on_initialize_motors).grid(
            row=2, column=2, sticky="w", pady=(10, 0)
        )
        ttk.Button(motors, text="Disconnect Motors", command=self.on_disconnect_motors).grid(
            row=2, column=3, sticky="w", pady=(10, 0)
        )
        ttk.Button(motors, text="STOP X+Y", command=self.on_stop_motors).grid(
            row=2, column=4, sticky="w", pady=(10, 0)
        )

        ttk.Label(motors, text="Step X (mm)").grid(row=3, column=0, sticky="w", pady=(10, 0))
        ttk.Entry(motors, textvariable=self.step_x_var, width=10).grid(row=3, column=1, sticky="w", pady=(10, 0))
        ttk.Button(motors, text="X -", command=lambda: self.on_motor_jog("X", -1, False)).grid(
            row=3, column=2, sticky="w", pady=(10, 0)
        )
        ttk.Button(motors, text="X +", command=lambda: self.on_motor_jog("X", +1, False)).grid(
            row=3, column=3, sticky="w", pady=(10, 0)
        )

        ttk.Label(motors, text="Step Y (mm)").grid(row=3, column=4, sticky="w", pady=(10, 0))
        ttk.Entry(motors, textvariable=self.step_y_var, width=10).grid(row=3, column=5, sticky="w", pady=(10, 0))
        ttk.Button(motors, text="Y -", command=lambda: self.on_motor_jog("Y", -1, False)).grid(
            row=3, column=6, sticky="w", pady=(10, 0)
        )
        ttk.Button(motors, text="Y +", command=lambda: self.on_motor_jog("Y", +1, False)).grid(
            row=3, column=7, sticky="w", pady=(10, 0)
        )

        ttk.Label(
            motors,
            text="Keyboard: Left/Right -> X-/X+, Up/Down -> Y+/Y-",
            foreground="gray",
        ).grid(row=4, column=0, columnspan=8, sticky="w", pady=(8, 0))

        camera = ttk.LabelFrame(root, text="Camera (Thorlabs LP126CU/M)", padding=10)
        camera.pack(fill="x", pady=(10, 0))

        ttk.Label(camera, text="Native DLL path").grid(row=0, column=0, sticky="w")
        ttk.Entry(camera, textvariable=self.camera_dll_path_var, width=88).grid(
            row=0, column=1, columnspan=5, sticky="ew", padx=6
        )
        ttk.Button(camera, text="Load Camera SDK", command=self.on_load_camera_sdk).grid(row=0, column=6, padx=6)
        ttk.Button(camera, text="Discover Camera", command=self.on_discover_camera).grid(
            row=1, column=6, padx=6, pady=(8, 0)
        )

        ttk.Label(camera, text="Camera Params").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Label(camera, text="Serial").grid(row=2, column=0, sticky="w", pady=(4, 0))
        self.serial_combo = ttk.Combobox(camera, textvariable=self.serial_var, width=36, state="readonly")
        self.serial_combo.grid(row=2, column=1, sticky="w", pady=(4, 0))
        ttk.Label(camera, text="Exposure (us)").grid(row=3, column=0, sticky="w", pady=(4, 0))
        ttk.Entry(camera, textvariable=self.exposure_var, width=12).grid(row=3, column=1, sticky="w", pady=(4, 0))
        ttk.Label(camera, text="Gain").grid(row=4, column=0, sticky="w", pady=(4, 0))
        ttk.Entry(camera, textvariable=self.gain_var, width=12).grid(row=4, column=1, sticky="w", pady=(4, 0))
        ttk.Label(camera, textvariable=self.gain_info_var, foreground="gray").grid(
            row=5, column=0, columnspan=2, sticky="w", pady=(4, 0)
        )
        ttk.Button(camera, text="Apply Params", command=self.on_apply_camera_params).grid(
            row=6, column=1, sticky="w", pady=(6, 0)
        )

        ttk.Button(camera, text="Connect Camera", command=self.on_connect_camera).grid(
            row=2, column=4, padx=6, pady=(4, 0)
        )
        ttk.Button(camera, text="Disconnect Camera", command=self.on_disconnect_camera).grid(
            row=2, column=5, padx=6, pady=(4, 0)
        )
        ttk.Button(camera, text="Start Live", command=self.on_start_live).grid(row=3, column=4, padx=6, pady=(4, 0))
        ttk.Button(camera, text="Stop Live", command=self.on_stop_live).grid(row=3, column=5, padx=6, pady=(4, 0))

        ttk.Label(camera, textvariable=self.fps_var).grid(row=2, column=2, sticky="w", pady=(4, 0))
        ttk.Label(camera, textvariable=self.status_var, foreground="blue").grid(
            row=2, column=3, columnspan=1, sticky="w", pady=(4, 0)
        )
        ttk.Label(camera, text="Point step (px)").grid(row=7, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(camera, textvariable=self.laser_step_var, width=8).grid(row=7, column=1, sticky="w", pady=(8, 0))
        ttk.Button(camera, text="X -", command=lambda: self.on_laser_move(-1, 0)).grid(
            row=7, column=2, sticky="w", pady=(8, 0)
        )
        ttk.Button(camera, text="X +", command=lambda: self.on_laser_move(+1, 0)).grid(
            row=7, column=3, sticky="w", pady=(8, 0)
        )
        ttk.Button(camera, text="Y -", command=lambda: self.on_laser_move(0, -1)).grid(
            row=7, column=4, sticky="w", pady=(8, 0)
        )
        ttk.Button(camera, text="Y +", command=lambda: self.on_laser_move(0, +1)).grid(
            row=7, column=5, sticky="w", pady=(8, 0)
        )
        ttk.Label(camera, textvariable=self.laser_coord_var).grid(row=7, column=6, sticky="w", pady=(8, 0))

        ttk.Label(camera, text="Point size (px)").grid(row=8, column=0, sticky="w", pady=(6, 0))
        ttk.Entry(camera, textvariable=self.laser_size_var, width=8).grid(row=8, column=1, sticky="w", pady=(6, 0))
        ttk.Button(camera, text="Size -", command=lambda: self.on_laser_size(-1)).grid(
            row=8, column=2, sticky="w", pady=(6, 0)
        )
        ttk.Button(camera, text="Size +", command=lambda: self.on_laser_size(+1)).grid(
            row=8, column=3, sticky="w", pady=(6, 0)
        )

        view = ttk.Frame(root)
        view.pack(fill="both", expand=True, pady=(10, 0))
        view.columnconfigure(0, weight=5)
        view.columnconfigure(1, weight=2)
        view.rowconfigure(0, weight=1)

        preview_box = ttk.LabelFrame(view, text="Live Preview", padding=10)
        preview_box.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        self.preview_label = ttk.Label(preview_box, text="No image", anchor="center")
        self.preview_label.pack(fill="both", expand=True)
        self.preview_label.bind("<Configure>", self._on_preview_resize)

        log_box = ttk.LabelFrame(view, text="Log", padding=10)
        log_box.grid(row=0, column=1, sticky="nsew")
        self.log_text = tk.Text(log_box, height=12, width=42)
        self.log_text.pack(fill="both", expand=True)

    def _bind_keyboard_controls(self):
        self.bind_all("<Left>", lambda event: self._on_arrow_key(event, "X", -1))
        self.bind_all("<Right>", lambda event: self._on_arrow_key(event, "X", +1))
        self.bind_all("<Up>", lambda event: self._on_arrow_key(event, "Y", +1))
        self.bind_all("<Down>", lambda event: self._on_arrow_key(event, "Y", -1))

    def _on_arrow_key(self, event, axis_name: str, direction: int):
        widget_class = event.widget.winfo_class() if event.widget is not None else ""
        if widget_class in {"Entry", "TEntry", "Text"}:
            return
        self.on_motor_jog(axis_name, direction, True)
        return "break"

    def _set_status(self, msg: str):
        self.status_var.set(msg)

    def _log(self, msg: str):
        stamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{stamp}] {msg}\n")
        self.log_text.see("end")

    def _run_async(self, title: str, worker):
        def run():
            try:
                self.after(0, lambda: self._set_status(f"{title}..."))
                worker()
                self.after(0, lambda: self._set_status(f"{title}: OK"))
            except Exception as exc:  # noqa: BLE001
                error_text = str(exc)
                self.after(0, lambda: self._set_status(f"{title}: ERROR"))
                self.after(0, lambda msg=error_text: self._log(f"{title} failed: {msg}"))

        threading.Thread(target=run, daemon=True).start()

    def _get_motor_timeout(self) -> float:
        value = float(self.motor_timeout_var.get())
        if value <= 0:
            raise ValueError
        return value

    def _get_motor_address(self) -> int:
        value = int(self.motor_addr_var.get())
        if value <= 0:
            raise ValueError
        return value

    def _axis_or_error(self, axis_name: str):
        axis = self.axis_x if axis_name == "X" else self.axis_y
        if axis is None or not axis.connected:
            raise ConexError(f"Axis {axis_name} not connected")
        return axis

    def on_load_motor_dll(self):
        path = self.motor_dll_var.get().strip()

        def worker():
            conex_class, loaded_from = load_conex_class(path)
            self.conex_class = conex_class
            self.loaded_motor_dll = loaded_from
            self.after(0, lambda: self._log(f"Motor DLL loaded from: {loaded_from}"))

        self._run_async("Load Motor DLL", worker)

    def on_scan_motors(self):
        def worker():
            if self.conex_class is None:
                raise ConexError("Load motor DLL first")
            tmp = self.conex_class()
            devices = sorted(set(str(port).strip() for port in (tmp.GetDevices() or []) if str(port).strip()))

            def apply_ports():
                self.x_combo["values"] = devices
                self.y_combo["values"] = devices
                if devices and self.x_port_var.get() not in devices:
                    self.x_port_var.set(devices[0])
                if len(devices) > 1 and self.y_port_var.get() not in devices:
                    self.y_port_var.set(devices[1])
                elif devices and self.y_port_var.get() not in devices:
                    self.y_port_var.set(devices[0])
                self._log(f"Motors detected: {devices}")

            self.after(0, apply_ports)

        self._run_async("Scan Motors", worker)

    def on_connect_motors(self):
        try:
            addr = self._get_motor_address()
        except ValueError:
            messagebox.showerror("Error", "Motor address must be a positive integer")
            return

        x_port = self.x_port_var.get().strip()
        y_port = self.y_port_var.get().strip()
        if self.conex_class is None:
            messagebox.showerror("Error", "Load motor DLL first")
            return
        if not x_port or not y_port:
            messagebox.showerror("Error", "Select both X and Y COM ports")
            return
        if x_port == y_port:
            messagebox.showerror("Error", "X and Y COM ports must be different")
            return

        def worker():
            self._disconnect_motors_impl(silent=True)
            # Requested mapping inversion: X command uses selected Y COM, and Y uses selected X COM.
            mapped_x_port = y_port
            mapped_y_port = x_port
            axis_x = ConexAxis("X", self.conex_class, mapped_x_port, address=addr)
            axis_y = ConexAxis("Y", self.conex_class, mapped_y_port, address=addr)
            axis_x.open()
            axis_y.open()
            self.axis_x = axis_x
            self.axis_y = axis_y
            self.after(
                0,
                lambda: self._log(
                    f"Motors connected (inverted COM mapping) X->{mapped_x_port}, Y->{mapped_y_port}, addr={addr}"
                ),
            )

        self._run_async("Connect Motors", worker)

    def on_initialize_motors(self):
        try:
            timeout_s = max(60.0, self._get_motor_timeout())
        except ValueError:
            messagebox.showerror("Error", "Timeout must be a positive number")
            return

        def worker():
            for axis_name in ("X", "Y"):
                axis = self._axis_or_error(axis_name)
                self.after(0, lambda name=axis_name: self._log(f"Initialize {name}: OR start"))
                axis.home(timeout_s=timeout_s)
                self.after(0, lambda name=axis_name: self._log(f"Initialize {name}: OR done"))

        self._run_async("Initialize Motors", worker)

    def on_stop_motors(self):
        def worker():
            for axis_name in ("X", "Y"):
                axis = self.axis_x if axis_name == "X" else self.axis_y
                if axis is not None and axis.connected:
                    axis.stop()
            self.after(0, lambda: self._log("STOP X+Y sent"))

        self._run_async("Stop Motors", worker)

    def _disconnect_motors_impl(self, silent=False):
        for axis in (self.axis_x, self.axis_y):
            if axis is None:
                continue
            try:
                axis.close()
            except Exception as exc:  # noqa: BLE001
                if not silent:
                    self.after(0, lambda msg=str(exc): self._log(f"Motor disconnect warning: {msg}"))
        self.axis_x = None
        self.axis_y = None

    def on_disconnect_motors(self):
        self._run_async("Disconnect Motors", lambda: self._disconnect_motors_impl(silent=False))

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
                messagebox.showerror("Error", f"Step {axis_name} must be a positive number")
            else:
                self._log(f"Step {axis_name} invalid")
            return

        def worker():
            axis = self._axis_or_error(axis_name)
            axis.move_relative(delta, timeout_s=timeout_s)
            self.after(0, lambda: self._log(f"Jog {axis_name}: {delta:+.4f} mm"))

        self._run_async(f"Jog {axis_name}", worker)

    def _native_dll_dir(self) -> Path:
        is_64bits = sys.maxsize > 2**32
        sub = "Native_64_lib" if is_64bits else "Native_32_lib"
        candidates = [
            Path(r"C:\Program Files\Thorlabs\ThorImageCAM"),
            Path(r"C:\Program Files\Thorlabs\ThorImageCAM") / "dlls" / ("64_lib" if is_64bits else "32_lib"),
            Path(r"C:\Program Files\Thorlabs\ThorImageCAM") / "Native Toolkit" / "dlls" / sub,
            self.project_root / "Camera" / "SDK" / "Native Toolkit" / "dlls" / sub,
        ]
        for candidate in candidates:
            if candidate.exists():
                return candidate
        return candidates[0]

    @staticmethod
    def _resolve_camera_dll_directory(path_hint: Path) -> Path:
        dll_name = "thorlabs_tsi_camera_sdk.dll"
        if path_hint.is_file() and path_hint.name.lower() == dll_name:
            return path_hint.parent
        if not path_hint.exists():
            raise RuntimeError(f"DLL folder not found: {path_hint}")
        direct = path_hint / dll_name
        if direct.exists():
            return path_hint
        matches = list(path_hint.rglob(dll_name))
        if matches:
            return matches[0].parent
        raise RuntimeError(f"'{dll_name}' not found under: {path_hint}")

    def _python_sdk_zip_subdir(self) -> Path:
        zip_path = self.project_root / "Camera" / "SDK" / "Python Toolkit" / "thorlabs_tsi_camera_python_sdk_package.zip"
        return Path(str(zip_path) + "\\thorlabs_tsi_sdk-0.0.8")

    def _configure_camera_runtime_paths(self):
        hint = Path(self.camera_dll_path_var.get().strip()) if self.camera_dll_path_var.get().strip() else self._native_dll_dir()
        native_dir = self._resolve_camera_dll_directory(hint)
        self.camera_dll_path_var.set(str(native_dir))
        os.environ["PATH"] = str(native_dir) + os.pathsep + os.environ.get("PATH", "")
        try:
            os.add_dll_directory(str(native_dir))
        except AttributeError:
            pass
        zip_subdir = self._python_sdk_zip_subdir()
        if zip_subdir.exists():
            sys.path.insert(0, str(zip_subdir))

    def _load_camera_sdk_modules(self):
        self._configure_camera_runtime_paths()
        try:
            from thorlabs_tsi_sdk.tl_camera import TLCameraSDK
            from thorlabs_tsi_sdk.tl_camera_enums import SENSOR_TYPE
            try:
                from thorlabs_tsi_sdk.tl_mono_to_color_processor import MonoToColorProcessorSDK
            except Exception:
                MonoToColorProcessorSDK = None
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(
                "Cannot import Thorlabs camera SDK Python package. Install:\n"
                "python -m pip install numpy pillow\n"
                "python -m pip install \"Camera/SDK/Python Toolkit/thorlabs_tsi_camera_python_sdk_package.zip\"\n"
                f"Error: {exc}"
            ) from exc
        self.TLCameraSDK = TLCameraSDK
        self.SENSOR_TYPE = SENSOR_TYPE
        self.MonoToColorProcessorSDK = MonoToColorProcessorSDK

    def on_load_camera_sdk(self):
        try:
            self._load_camera_sdk_modules()
            self._set_status("Camera SDK loaded")
            self._log("Camera SDK loaded.")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Camera SDK Error", str(exc))

    def on_discover_camera(self):
        try:
            if self.TLCameraSDK is None:
                self._load_camera_sdk_modules()
            if self.sdk is None:
                self.sdk = self.TLCameraSDK()
            serials = list(self.sdk.discover_available_cameras())
            self.serial_combo["values"] = serials
            if serials:
                self.serial_var.set(serials[0])
                self._set_status(f"{len(serials)} camera(s) detected")
                self._log(f"Cameras detected: {serials}")
            else:
                self.serial_var.set("")
                self._set_status("No camera detected")
                self._log("No camera detected.")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Discover Camera Error", str(exc))

    def on_connect_camera(self):
        try:
            if self.sdk is None:
                self.on_discover_camera()
            serial = self.serial_var.get().strip()
            if not serial:
                raise RuntimeError("No camera selected")
            if self.camera is not None:
                self.on_disconnect_camera()
            self.camera = self.sdk.open_camera(serial)
            self.exposure_var.set(str(int(self.camera.exposure_time_us)))
            self._refresh_gain_from_camera()
            self._set_status(f"Camera connected: {serial}")
            self._log(
                f"Camera connected model={self.camera.model} "
                f"res={self.camera.image_width_pixels}x{self.camera.image_height_pixels}"
            )
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Connect Camera Error", str(exc))

    def _refresh_gain_from_camera(self):
        if self.camera is None:
            self.gain_info_var.set("Gain: unknown")
            return
        try:
            gain_range = self.camera.gain_range
            gain_min = int(gain_range.min)
            gain_max = int(gain_range.max)
            if gain_max <= 0:
                self.gain_info_var.set("Gain: not supported")
                self.gain_var.set("0")
                return
            current_gain = int(self.camera.gain)
            self.gain_var.set(str(current_gain))
            try:
                db_value = float(self.camera.convert_gain_to_decibels(current_gain))
                self.gain_info_var.set(f"Gain range: {gain_min}..{gain_max} ({db_value:.2f} dB)")
            except Exception:
                self.gain_info_var.set(f"Gain range: {gain_min}..{gain_max}")
        except Exception:
            self.gain_info_var.set("Gain: unavailable")

    def on_apply_camera_params(self):
        try:
            self._apply_camera_params_checked()
            self._refresh_gain_from_camera()
            self._log("Camera params applied.")
            self._set_status("Camera params applied")
        except ValueError:
            messagebox.showerror("Error", "Exposure/Gain must be valid numbers")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Apply Camera Params Error", str(exc))

    def _apply_camera_params_checked(self):
        if self.camera is None:
            raise RuntimeError("Camera not connected")
        exposure_us = int(float(self.exposure_var.get()))
        if exposure_us <= 0:
            raise ValueError
        self.camera.exposure_time_us = exposure_us

        gain_range = self.camera.gain_range
        gain_max = int(gain_range.max)
        if gain_max > 0:
            gain_value = int(float(self.gain_var.get()))
            gain_min = int(gain_range.min)
            if gain_value < gain_min or gain_value > gain_max:
                raise RuntimeError(f"Gain must be in range [{gain_min}, {gain_max}]")
            self.camera.gain = gain_value

    def on_start_live(self):
        try:
            if self.camera is None:
                raise RuntimeError("Camera not connected")
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
            self._set_status("Live streaming")
            self._log("Live started.")
            self._schedule_preview_update()
        except ValueError:
            messagebox.showerror("Error", "Exposure/Gain parameters are invalid")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Start Live Error", str(exc))

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
        self._set_status("Live stopped")
        self._log("Live stopped.")

    def on_disconnect_camera(self):
        self.on_stop_live()
        if self.camera is not None:
            try:
                self.camera.dispose()
            finally:
                self.camera = None
        self._set_status("Camera disconnected")
        self._log("Camera disconnected.")

    def _schedule_preview_update(self):
        if self._preview_job is None:
            self._preview_loop()

    def _update_laser_label(self):
        if not self.laser_initialized:
            self.laser_coord_var.set("Laser: X=- Y=-")
            return
        self.laser_coord_var.set(f"Laser: X={self.laser_x} Y={self.laser_y}")

    def _clamp_laser_to_frame(self):
        if self._last_frame_width <= 0 or self._last_frame_height <= 0:
            return
        self.laser_x = max(0, min(self._last_frame_width - 1, int(self.laser_x)))
        self.laser_y = max(0, min(self._last_frame_height - 1, int(self.laser_y)))

    def _init_laser_if_needed(self):
        if self.laser_initialized:
            return
        if self._last_frame_width <= 0 or self._last_frame_height <= 0:
            return
        self.laser_x = self._last_frame_width // 2
        self.laser_y = self._last_frame_height // 2
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
        x = int(self.laser_x)
        y = int(self.laser_y)
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), outline=(255, 0, 0), width=2)
        draw.ellipse((x - 1, y - 1, x + 1, y + 1), fill=(255, 0, 0))
        return image

    def on_laser_move(self, dx: int, dy: int):
        try:
            step = int(float(self.laser_step_var.get()))
        except ValueError:
            messagebox.showerror("Error", "Point step must be a positive integer")
            return
        if step <= 0:
            messagebox.showerror("Error", "Point step must be a positive integer")
            return
        self._init_laser_if_needed()
        if not self.laser_initialized:
            self._log("Start live stream to initialize image coordinates")
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

    def _fit_image_to_preview(self, image: Image.Image) -> Image.Image:
        target_w = max(self.preview_label.winfo_width(), 1)
        target_h = max(self.preview_label.winfo_height(), 1)
        src_w, src_h = image.size
        if src_w <= 0 or src_h <= 0:
            return image
        scale = min(target_w / src_w, target_h / src_h)
        if scale <= 0:
            return image
        new_w = max(int(src_w * scale), 1)
        new_h = max(int(src_h * scale), 1)
        if new_w == src_w and new_h == src_h:
            return image
        try:
            resample = Image.Resampling.LANCZOS
        except AttributeError:
            resample = Image.LANCZOS
        return image.resize((new_w, new_h), resample)

    def _render_preview(self, image: Image.Image):
        display_image = self._draw_laser_overlay(image.copy())
        fitted = self._fit_image_to_preview(display_image)
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
            if elapsed >= 1.0:
                fps = self._frame_counter / elapsed
                self.fps_var.set(f"FPS: {fps:.1f}")
                self._frame_counter = 0
                self._fps_t0 = time.monotonic()
        except queue.Empty:
            pass
        self._preview_job = self.after(15, self._preview_loop)

    def _dispose_camera_sdk(self):
        if self.sdk is not None:
            try:
                self.sdk.dispose()
            finally:
                self.sdk = None

    def on_close(self):
        try:
            self.on_disconnect_camera()
            self._disconnect_motors_impl(silent=True)
        finally:
            self._dispose_camera_sdk()
            self.destroy()


def main():
    app = MotorCameraComboApp()
    app.mainloop()


if __name__ == "__main__":
    main()
