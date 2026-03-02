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
    from PIL import Image, ImageTk
except Exception as dep_exc:  # noqa: BLE001
    print("Missing Python dependencies.")
    print("Install with:")
    print("  python -m pip install numpy pillow")
    raise SystemExit(dep_exc) from dep_exc


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


class ThorlabsCameraTestApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Thorlabs LP126CU/M - Test Flux Video")
        self.geometry("1120x760")
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.project_root = Path(__file__).resolve().parent.parent
        self.sdk = None
        self.camera = None
        self.stream_thread = None
        self.image_queue = queue.Queue(maxsize=2)
        self.preview_photo = None
        self._last_image = None
        self._preview_job = None
        self._frame_counter = 0
        self._fps_t0 = time.monotonic()

        self.TLCameraSDK = None
        self.SENSOR_TYPE = None
        self.MonoToColorProcessorSDK = None

        self.serial_var = tk.StringVar(value="")
        self.status_var = tk.StringVar(value="Ready")
        self.exposure_var = tk.StringVar(value="10000")
        self.fps_var = tk.StringVar(value="FPS: -")
        self.dll_path_var = tk.StringVar(value=str(self._native_dll_dir()))

        self._build_ui()
        self._log("Application prête.")

    def _build_ui(self):
        root = ttk.Frame(self, padding=10)
        root.pack(fill="both", expand=True)

        ctrl = ttk.LabelFrame(root, text="Connexion Camera", padding=10)
        ctrl.pack(fill="x")

        ttk.Label(ctrl, text="Native DLL path").grid(row=0, column=0, sticky="w")
        ttk.Entry(ctrl, textvariable=self.dll_path_var, width=90).grid(
            row=0, column=1, columnspan=5, sticky="ew", padx=6
        )

        ttk.Button(ctrl, text="Load SDK", command=self.on_load_sdk).grid(row=0, column=6, padx=6)
        ttk.Button(ctrl, text="Discover", command=self.on_discover).grid(row=1, column=6, padx=6, pady=(8, 0))

        ttk.Label(ctrl, text="Serial").grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.serial_combo = ttk.Combobox(ctrl, textvariable=self.serial_var, width=35, state="readonly")
        self.serial_combo.grid(row=1, column=1, sticky="w", pady=(8, 0))

        ttk.Label(ctrl, text="Exposure (us)").grid(row=1, column=2, sticky="e", pady=(8, 0))
        ttk.Entry(ctrl, textvariable=self.exposure_var, width=10).grid(row=1, column=3, sticky="w", pady=(8, 0))

        ttk.Button(ctrl, text="Connect", command=self.on_connect).grid(row=1, column=4, padx=6, pady=(8, 0))
        ttk.Button(ctrl, text="Disconnect", command=self.on_disconnect).grid(row=1, column=5, padx=6, pady=(8, 0))
        ttk.Button(ctrl, text="Start Live", command=self.on_start_live).grid(row=2, column=4, padx=6, pady=(8, 0))
        ttk.Button(ctrl, text="Stop Live", command=self.on_stop_live).grid(row=2, column=5, padx=6, pady=(8, 0))

        ttk.Label(ctrl, textvariable=self.fps_var).grid(row=2, column=1, sticky="w", pady=(8, 0))
        ttk.Label(ctrl, textvariable=self.status_var, foreground="blue").grid(
            row=2, column=2, columnspan=2, sticky="w", pady=(8, 0)
        )

        preview_box = ttk.LabelFrame(root, text="Flux Video", padding=10)
        preview_box.pack(fill="both", expand=True, pady=(10, 0))
        self.preview_label = ttk.Label(preview_box, text="Aucune image")
        self.preview_label.pack(fill="both", expand=True)
        self.preview_label.bind("<Configure>", self._on_preview_resize)

        log_box = ttk.LabelFrame(root, text="Log", padding=10)
        log_box.pack(fill="both", expand=False, pady=(10, 0))
        self.log_text = tk.Text(log_box, height=10, width=120)
        self.log_text.pack(fill="both", expand=True)

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
    def _resolve_dll_directory(path_hint: Path) -> Path:
        """
        Resolve to the directory that actually contains thorlabs_tsi_camera_sdk.dll.
        Accepts either the exact DLL folder or a parent folder.
        """
        dll_name = "thorlabs_tsi_camera_sdk.dll"
        if path_hint.is_file() and path_hint.name.lower() == dll_name:
            return path_hint.parent
        if not path_hint.exists():
            raise RuntimeError(f"DLL folder not found: {path_hint}")

        direct = path_hint / dll_name
        if direct.exists():
            return path_hint

        # Search descendants (covers ThorImageCAM root paths).
        matches = list(path_hint.rglob(dll_name))
        if matches:
            return matches[0].parent

        raise RuntimeError(
            f"'{dll_name}' not found in: {path_hint}\n"
            "Point 'Native DLL path' to the directory containing this DLL."
        )

    def _python_sdk_zip_subdir(self) -> Path:
        zip_path = self.project_root / "Camera" / "SDK" / "Python Toolkit" / "thorlabs_tsi_camera_python_sdk_package.zip"
        return Path(str(zip_path) + "\\thorlabs_tsi_sdk-0.0.8")

    def _configure_runtime_paths(self):
        native_hint = Path(self.dll_path_var.get().strip()) if self.dll_path_var.get().strip() else self._native_dll_dir()
        native_dir = self._resolve_dll_directory(native_hint)
        self.dll_path_var.set(str(native_dir))

        os.environ["PATH"] = str(native_dir) + os.pathsep + os.environ.get("PATH", "")
        try:
            os.add_dll_directory(str(native_dir))
        except AttributeError:
            pass

        zip_subdir = self._python_sdk_zip_subdir()
        if zip_subdir.exists():
            sys.path.insert(0, str(zip_subdir))

    def _load_sdk_modules(self):
        self._configure_runtime_paths()
        try:
            from thorlabs_tsi_sdk.tl_camera import TLCameraSDK
            from thorlabs_tsi_sdk.tl_camera_enums import SENSOR_TYPE
            try:
                from thorlabs_tsi_sdk.tl_mono_to_color_processor import MonoToColorProcessorSDK
            except Exception:
                MonoToColorProcessorSDK = None
        except Exception as exc:  # noqa: BLE001
            install_hint = (
                "Impossible d'importer le SDK Python Thorlabs.\n\n"
                "Installe les paquets dans ton venv:\n"
                "1) python -m pip install numpy pillow\n"
                "2) python -m pip install \"Camera/SDK/Python Toolkit/thorlabs_tsi_camera_python_sdk_package.zip\"\n"
            )
            raise RuntimeError(f"{install_hint}\nErreur: {exc}") from exc

        self.TLCameraSDK = TLCameraSDK
        self.SENSOR_TYPE = SENSOR_TYPE
        self.MonoToColorProcessorSDK = MonoToColorProcessorSDK

    def _set_status(self, msg: str):
        self.status_var.set(msg)

    def _log(self, msg: str):
        t = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{t}] {msg}\n")
        self.log_text.see("end")

    def on_load_sdk(self):
        try:
            self._load_sdk_modules()
            self._set_status("SDK loaded")
            self._log("SDK Thorlabs charge avec succes.")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("SDK Error", str(exc))
            self._set_status("SDK error")

    def on_discover(self):
        try:
            if self.TLCameraSDK is None:
                self._load_sdk_modules()

            if self.sdk is None:
                self.sdk = self.TLCameraSDK()

            serials = list(self.sdk.discover_available_cameras())
            self.serial_combo["values"] = serials
            if serials:
                self.serial_var.set(serials[0])
                self._set_status(f"{len(serials)} camera(s) detected")
                self._log(f"Cameras detectees: {serials}")
            else:
                self.serial_var.set("")
                self._set_status("No camera detected")
                self._log("Aucune camera detectee.")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Discover Error", str(exc))
            self._set_status("Discover error")

    def on_connect(self):
        try:
            if self.sdk is None:
                self.on_discover()
            serial = self.serial_var.get().strip()
            if not serial:
                raise RuntimeError("Aucune camera selectionnee.")
            if self.camera is not None:
                self.on_disconnect()
            self.camera = self.sdk.open_camera(serial)
            self._set_status(f"Connected: {serial}")
            self._log(f"Camera connectee: {serial}")
            self._log(
                f"Modele={self.camera.model}, capteur={self.camera.camera_sensor_type}, "
                f"res={self.camera.image_width_pixels}x{self.camera.image_height_pixels}, "
                f"bit_depth={self.camera.bit_depth}"
            )
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Connect Error", str(exc))
            self._set_status("Connect error")

    def on_start_live(self):
        try:
            if self.camera is None:
                raise RuntimeError("Camera non connectee.")
            if self.stream_thread is not None:
                return
            exposure_us = int(float(self.exposure_var.get()))
            if exposure_us <= 0:
                raise ValueError

            self.camera.exposure_time_us = exposure_us
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
            self._log("Flux live demarre.")
            self._schedule_preview_update()
        except ValueError:
            messagebox.showerror("Value Error", "Exposure must be a positive integer (us).")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Start Error", str(exc))
            self._set_status("Start error")

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
        self._log("Flux live arrete.")

    def _schedule_preview_update(self):
        if self._preview_job is None:
            self._preview_loop()

    def _fit_image_to_preview(self, image: Image.Image) -> Image.Image:
        target_w = max(self.preview_label.winfo_width(), 1)
        target_h = max(self.preview_label.winfo_height(), 1)
        src_w, src_h = image.size
        if src_w <= 0 or src_h <= 0:
            return image

        scale = min(target_w / src_w, target_h / src_h)
        # Keep original size if preview area is not ready yet.
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
        fitted = self._fit_image_to_preview(image)
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

    def on_disconnect(self):
        self.on_stop_live()
        if self.camera is not None:
            try:
                self.camera.dispose()
            finally:
                self.camera = None
        self._set_status("Disconnected")
        self._log("Camera deconnectee.")

    def _dispose_sdk(self):
        if self.sdk is not None:
            try:
                self.sdk.dispose()
            finally:
                self.sdk = None

    def on_close(self):
        try:
            self.on_disconnect()
        finally:
            self._dispose_sdk()
            self.destroy()


def main():
    app = ThorlabsCameraTestApp()
    app.mainloop()


if __name__ == "__main__":
    main()
