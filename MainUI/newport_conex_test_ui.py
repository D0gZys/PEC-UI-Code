import threading
import time
import traceback
from dataclasses import dataclass
from pathlib import Path
import tkinter as tk
from tkinter import messagebox, ttk

try:
    import clr  # type: ignore
except ModuleNotFoundError:
    clr = None


_PROJECT_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_DLL_CANDIDATES = [
    str(_PROJECT_ROOT / "MotorController" / "lib" / "Newport.CONEXCC.CommandInterface.dll"),
    r"C:\Windows\Microsoft.NET\assembly\GAC_64\Newport.CONEXCC.CommandInterface\v4.0_2.0.0.3__aab368c79b10b8be\Newport.CONEXCC.CommandInterface.dll",
    r"C:\Newport\Motion Control\CONEX-CC\Bin\64-bit\Newport.CONEXCC.CommandInterface.dll",
    r"C:\Newport\Motion Control\CONEX-CC\Bin\Newport.CONEXCC.CommandInterface.dll",
]

ABS_MIN_MM = 0.0
ABS_MAX_MM = 25.0
LIVE_MIN_SPEED_MM_S = 0.01
LIVE_MAX_SPEED_MM_S = 1.00
WHEEL_STEP_MM = 0.05
WHEEL_STEP_FINE_MM = 0.01
WHEEL_STEP_FAST_MM = 0.20

NOT_REFERENCED_STATES = {"0A", "0B", "0C", "0D", "0E", "0F"}
BUSY_STATES = {"1E", "1F", "28", "29", "2A", "2B", "46", "47"}
DISABLED_STATES = {"3C", "3D"}

STATE_LABELS = {
    "0A": "Not referenced from reset",
    "0B": "Not referenced from homing",
    "0C": "Not referenced from moving",
    "0D": "Not referenced from disable",
    "0E": "Not referenced from jog",
    "0F": "Not referenced",
    "14": "Configuration",
    "1E": "Homing",
    "1F": "Homing (2)",
    "28": "Moving",
    "29": "Stepping",
    "2A": "Jogging",
    "2B": "Tracking",
    "32": "Ready from reset",
    "33": "Ready from homing",
    "34": "Ready from moving",
    "35": "Ready from disable",
    "36": "Ready from jog",
    "37": "Ready from tracking",
    "3C": "Disable from ready",
    "3D": "Disable from moving",
    "46": "Motion process",
    "47": "Motion process",
}


def normalize_state_code(code: str) -> str:
    return (code or "").strip().upper()


def state_to_text(code: str) -> str:
    code = normalize_state_code(code)
    if not code:
        return "Unknown"
    return f"{code} - {STATE_LABELS.get(code, 'Unknown state')}"


class ConexError(RuntimeError):
    pass


def load_conex_class(dll_path: str):
    if clr is None:
        raise ConexError(
            "pythonnet is not installed. Install it first with: py -3.11 -m pip install pythonnet"
        )

    add_errors = []
    loaded_from = None

    def try_add_reference(ref: str, by_path: bool):
        nonlocal loaded_from
        try:
            if by_path:
                clr.AddReference(str(ref))
            else:
                clr.AddReference(ref)
            loaded_from = ref
            return True
        except Exception as exc:  # noqa: BLE001
            add_errors.append(f"{ref}: {exc}")
            return False

    # Priority 1: user path if provided.
    user_path = (dll_path or "").strip()
    if user_path:
        if Path(user_path).exists():
            if try_add_reference(user_path, by_path=True):
                pass
        else:
            add_errors.append(f"{user_path}: file not found")

    # Priority 2: assembly name from GAC.
    if loaded_from is None:
        try_add_reference("Newport.CONEXCC.CommandInterface", by_path=False)

    # Priority 3: known local file paths.
    if loaded_from is None:
        for candidate in DEFAULT_DLL_CANDIDATES:
            if Path(candidate).exists() and try_add_reference(candidate, by_path=True):
                break

    if loaded_from is None:
        details = "\n".join(add_errors[-5:])
        raise ConexError(
            "Unable to load Newport.CONEXCC.CommandInterface.dll.\n"
            "Check Newport software installation and DLL path.\n"
            f"Last errors:\n{details}"
        )

    try:
        from CommandInterfaceConexCC import ConexCC  # type: ignore
    except Exception as exc:  # noqa: BLE001
        raise ConexError(
            f"DLL loaded from '{loaded_from}', but class CommandInterfaceConexCC.ConexCC is unavailable: {exc}"
        ) from exc

    return ConexCC, str(loaded_from)


@dataclass
class AxisSnapshot:
    connected: bool = False
    port: str = ""
    position: float | None = None
    error_code: str = ""
    state_code: str = ""
    issue: str = ""


class ConexAxis:
    def __init__(self, axis_name: str, conex_class, port: str, address: int = 1):
        self.axis_name = axis_name
        self.port = port
        self.address = int(address)
        self._conex_class = conex_class
        self._api = None
        self._lock = threading.RLock()
        self.connected = False

    def _normalize_out(self, value, out_type: str):
        if out_type == "str":
            return "" if value is None else str(value).strip()
        if out_type == "float":
            if value is None:
                return 0.0
            return float(value)
        return value

    def _call(self, method_name: str, *args, out_types=()):
        if self._api is None:
            raise ConexError(f"{self.axis_name}: controller is not instantiated")

        method = getattr(self._api, method_name)
        placeholders = [0.0 if t == "float" else "" for t in out_types]

        # pythonnet tuple style.
        try:
            result = method(*args, *placeholders)
            if isinstance(result, tuple):
                ret = int(result[0])
                outs = [
                    self._normalize_out(result[idx + 1], out_types[idx])
                    for idx in range(len(out_types))
                ]
                if len(outs) == len(out_types):
                    return (ret, *outs)
            elif not out_types:
                return int(result)
        except TypeError:
            pass

        # pythonnet clr.Reference style.
        try:
            from System import Double, String  # type: ignore
        except Exception as exc:  # noqa: BLE001
            raise ConexError(f"{self.axis_name}: failed to import .NET types: {exc}") from exc

        refs = []
        for out_type in out_types:
            if out_type == "str":
                refs.append(clr.Reference[String]())
            elif out_type == "float":
                refs.append(clr.Reference[Double](0.0))
            else:
                raise ConexError(f"{self.axis_name}: unsupported out type '{out_type}'")

        result = method(*args, *refs)
        ret = int(result[0]) if isinstance(result, tuple) else int(result)
        outs = [self._normalize_out(ref.Value, out_types[idx]) for idx, ref in enumerate(refs)]
        return (ret, *outs)

    def _diagnostic_text(self) -> str:
        if not self.connected:
            return ""
        try:
            te_ret, last_error, te_err = self._call("TE", self.address, out_types=("str", "str"))
            if te_ret != 0:
                return f"TE failed ({te_ret}): {te_err}"
            last_error = (last_error or "").strip()
            if not last_error or last_error == "0":
                return ""
            tb_ret, out_error, tb_err = self._call(
                "TB", self.address, last_error, out_types=("str", "str")
            )
            if tb_ret != 0:
                return f"TE={last_error}; TB failed ({tb_ret}): {tb_err}"
            return f"TE={last_error}; TB={out_error}"
        except Exception as exc:  # noqa: BLE001
            return f"Diagnostic unavailable: {exc}"

    def _raise_if_failed(self, command: str, ret_code: int, err_string: str):
        if ret_code == 0:
            return
        diag = self._diagnostic_text()
        detail = f"{self.axis_name}: {command} failed ({ret_code})"
        if err_string:
            detail += f" - {err_string}"
        if diag:
            detail += f" | {diag}"
        raise ConexError(detail)

    def open(self):
        with self._lock:
            if self.connected:
                return
            self._api = self._conex_class()
            ret = int(self._api.OpenInstrument(self.port))
            if ret != 0:
                self._api = None
                raise ConexError(
                    f"{self.axis_name}: OpenInstrument({self.port}) failed ({ret}). "
                    "Check COM port and ensure Newport MotionControl is not connected."
                )
            self.connected = True

    def close(self):
        with self._lock:
            if self._api is None:
                self.connected = False
                return
            try:
                ret = int(self._api.CloseInstrument())
                if ret != 0:
                    raise ConexError(f"{self.axis_name}: CloseInstrument failed ({ret})")
            finally:
                self.connected = False
                self._api = None

    def stop(self):
        with self._lock:
            if not self.connected:
                return
            ret, err = self._call("ST", self.address, out_types=("str",))
            self._raise_if_failed("ST", ret, err)

    def read_state(self):
        with self._lock:
            if not self.connected:
                raise ConexError(f"{self.axis_name}: axis not connected")
            ret, error_code, state_code, err = self._call(
                "TS", self.address, out_types=("str", "str", "str")
            )
            self._raise_if_failed("TS", ret, err)
            return (normalize_state_code(error_code), normalize_state_code(state_code))

    def read_position(self):
        with self._lock:
            if not self.connected:
                raise ConexError(f"{self.axis_name}: axis not connected")
            ret, position, err = self._call("TP", self.address, out_types=("float", "str"))
            self._raise_if_failed("TP", ret, err)
            return float(position)

    def wait_done(self, timeout_s=30.0, poll_s=0.1):
        deadline = time.monotonic() + float(timeout_s)
        last_state = ""
        while time.monotonic() < deadline:
            _, state = self.read_state()
            last_state = state
            if state in BUSY_STATES:
                time.sleep(poll_s)
                continue
            if state in DISABLED_STATES:
                raise ConexError(f"{self.axis_name}: axis is disabled ({state_to_text(state)})")
            return
        raise ConexError(
            f"{self.axis_name}: timeout while waiting for motion complete (last state={last_state})"
        )

    def home(self, timeout_s=90.0):
        with self._lock:
            ret, err = self._call("OR", self.address, out_types=("str",))
            self._raise_if_failed("OR", ret, err)
        self.wait_done(timeout_s=timeout_s, poll_s=0.2)
        _, state_code = self.read_state()
        if state_code in NOT_REFERENCED_STATES:
            raise ConexError(
                f"{self.axis_name}: home finished but axis is still not referenced ({state_to_text(state_code)})"
            )
        return state_code

    def move_absolute(self, position: float, timeout_s=30.0):
        with self._lock:
            ret, err = self._call("PA_Set", self.address, float(position), out_types=("str",))
            self._raise_if_failed("PA_Set", ret, err)
        self.wait_done(timeout_s=timeout_s, poll_s=0.1)

    def move_absolute_no_wait(self, position: float):
        with self._lock:
            ret, err = self._call("PA_Set", self.address, float(position), out_types=("str",))
            self._raise_if_failed("PA_Set", ret, err)

    def set_velocity(self, velocity_mm_s: float):
        with self._lock:
            ret, err = self._call("VA_Set", self.address, float(velocity_mm_s), out_types=("str",))
            self._raise_if_failed("VA_Set", ret, err)

    def move_relative(self, delta: float, timeout_s=30.0):
        with self._lock:
            ret, err = self._call("PR_Set", self.address, float(delta), out_types=("str",))
            self._raise_if_failed("PR_Set", ret, err)
        self.wait_done(timeout_s=timeout_s, poll_s=0.1)

    def snapshot(self) -> AxisSnapshot:
        if not self.connected:
            return AxisSnapshot(connected=False, port=self.port)
        if not self._lock.acquire(blocking=False):
            return AxisSnapshot(connected=True, port=self.port, issue="busy")
        try:
            error_code, state_code = self.read_state()
            position = self.read_position()
            return AxisSnapshot(
                connected=True,
                port=self.port,
                position=position,
                error_code=error_code,
                state_code=state_code,
                issue="",
            )
        except Exception as exc:  # noqa: BLE001
            return AxisSnapshot(connected=True, port=self.port, issue=str(exc))
        finally:
            self._lock.release()


class AxisLiveCommander:
    """Background sender for live slider tracking on one axis."""

    def __init__(self, axis: ConexAxis, axis_name: str, speed_getter, log_callback):
        self.axis = axis
        self.axis_name = axis_name
        self._get_speed = speed_getter
        self._log_callback = log_callback
        self._target = None
        self._last_sent_target = None
        self._last_set_speed = None
        self._last_error = ""
        self._last_error_ts = 0.0
        self._lock = threading.Lock()
        self._wake_event = threading.Event()
        self._stop_event = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def set_target(self, target_mm: float):
        with self._lock:
            self._target = float(target_mm)
        self._wake_event.set()

    def stop(self):
        self._stop_event.set()
        self._wake_event.set()
        self._thread.join(timeout=1.0)

    def _loop(self):
        while not self._stop_event.is_set():
            self._wake_event.wait(0.05)  # ~20 Hz update max
            self._wake_event.clear()
            if self._stop_event.is_set():
                break

            with self._lock:
                target = self._target

            if target is None:
                continue

            try:
                speed = float(self._get_speed())
                if self._last_set_speed is None or abs(speed - self._last_set_speed) > 1e-6:
                    self.axis.set_velocity(speed)
                    self._last_set_speed = speed

                if self._last_sent_target is None or abs(target - self._last_sent_target) >= 0.001:
                    self.axis.move_absolute_no_wait(target)
                    self._last_sent_target = target
            except Exception as exc:  # noqa: BLE001
                msg = str(exc)
                now = time.monotonic()
                if msg != self._last_error or (now - self._last_error_ts) > 1.0:
                    self._log_callback(f"Live {self.axis_name}: {msg}")
                    self._last_error = msg
                    self._last_error_ts = now
                time.sleep(0.1)


class ConexTestApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Newport CONEX-CC - Test Control UI")
        self.geometry("980x660")

        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.conex_class = None
        self.loaded_dll = ""
        self.axis_x = None
        self.axis_y = None
        self._live_commanders = {}
        self._status_job = None
        self._closing = False
        self._suspend_slider_callback = False

        default_dll = ""
        for candidate in DEFAULT_DLL_CANDIDATES:
            if Path(candidate).exists():
                default_dll = candidate
                break

        self.dll_path_var = tk.StringVar(value=default_dll)
        self.x_port_var = tk.StringVar()
        self.y_port_var = tk.StringVar()
        self.timeout_var = tk.StringVar(value="30")
        self.addr_var = tk.StringVar(value="1")

        self.abs_x_var = tk.StringVar(value="0.0")
        self.abs_y_var = tk.StringVar(value="0.0")
        self.step_x_var = tk.StringVar(value="0.1")
        self.step_y_var = tk.StringVar(value="0.1")
        self.slider_x_var = tk.DoubleVar(value=0.0)
        self.slider_y_var = tk.DoubleVar(value=0.0)
        self.slider_x_label_var = tk.StringVar(value="0.000 mm")
        self.slider_y_label_var = tk.StringVar(value="0.000 mm")
        self.live_speed_mm_s = 0.30
        self.live_speed_var = tk.DoubleVar(value=self.live_speed_mm_s)
        self.live_speed_label_var = tk.StringVar(value=f"{self.live_speed_mm_s:.3f} mm/s")

        self._build_ui()
        self._set_status("Ready")

    def _build_ui(self):
        root = ttk.Frame(self, padding=10)
        root.pack(fill="both", expand=True)

        conn = ttk.LabelFrame(root, text="1) DLL + Connexion moteurs", padding=10)
        conn.pack(fill="x")

        ttk.Label(conn, text="DLL path").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn, textvariable=self.dll_path_var, width=78).grid(
            row=0, column=1, columnspan=4, sticky="ew", padx=6
        )

        ttk.Button(conn, text="Load DLL", command=self.on_load_dll).grid(row=0, column=5, padx=6)
        ttk.Button(conn, text="Scan devices", command=self.on_scan_devices).grid(
            row=0, column=6, padx=6
        )

        ttk.Label(conn, text="Axis X COM").grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.x_combo = ttk.Combobox(conn, textvariable=self.x_port_var, width=20, state="readonly")
        self.x_combo.grid(row=1, column=1, sticky="w", pady=(8, 0))

        ttk.Label(conn, text="Axis Y COM").grid(row=1, column=2, sticky="w", pady=(8, 0))
        self.y_combo = ttk.Combobox(conn, textvariable=self.y_port_var, width=20, state="readonly")
        self.y_combo.grid(row=1, column=3, sticky="w", pady=(8, 0))

        ttk.Label(conn, text="Address").grid(row=1, column=4, sticky="e", pady=(8, 0))
        ttk.Entry(conn, textvariable=self.addr_var, width=6).grid(row=1, column=5, sticky="w", pady=(8, 0))

        ttk.Button(conn, text="Connect X+Y", command=self.on_connect).grid(
            row=2, column=1, sticky="w", pady=(10, 0)
        )
        ttk.Button(conn, text="Initialize (Home X+Y)", command=self.on_initialize).grid(
            row=2, column=2, sticky="w", pady=(10, 0)
        )
        ttk.Button(conn, text="Disconnect", command=self.on_disconnect).grid(
            row=2, column=3, sticky="w", pady=(10, 0)
        )
        ttk.Button(conn, text="STOP X+Y", command=self.on_stop_all).grid(
            row=2, column=4, sticky="w", pady=(10, 0)
        )
        ttk.Button(conn, text="Debug TS", command=self.on_debug_ts).grid(
            row=2, column=5, sticky="w", pady=(10, 0)
        )

        ctrl = ttk.LabelFrame(root, text="2) Controle axes", padding=10)
        ctrl.pack(fill="x", pady=(10, 0))

        ttk.Label(ctrl, text="Timeout (s)").grid(row=0, column=0, sticky="w")
        ttk.Entry(ctrl, textvariable=self.timeout_var, width=8).grid(row=0, column=1, sticky="w", padx=(4, 20))
        ttk.Label(ctrl, text="Vitesse slider (mm/s)").grid(row=0, column=2, sticky="w")
        self.live_speed_slider = ttk.Scale(
            ctrl,
            from_=LIVE_MIN_SPEED_MM_S,
            to=LIVE_MAX_SPEED_MM_S,
            orient="horizontal",
            variable=self.live_speed_var,
            command=self.on_live_speed_change,
            length=180,
        )
        self.live_speed_slider.grid(row=0, column=3, columnspan=2, sticky="we", padx=(4, 0))
        ttk.Label(ctrl, textvariable=self.live_speed_label_var, width=12).grid(
            row=0, column=5, sticky="w", padx=(8, 0)
        )

        ttk.Label(ctrl, text="Axe").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Label(ctrl, text="Move absolu").grid(row=1, column=1, sticky="w", pady=(8, 0))
        ttk.Label(ctrl, text="Pas relatif").grid(row=1, column=3, sticky="w", pady=(8, 0))
        ttk.Label(ctrl, text="Jog").grid(row=1, column=4, sticky="w", pady=(8, 0))
        ttk.Label(ctrl, text="Slider (0..25 mm)").grid(row=1, column=6, sticky="w", pady=(8, 0))

        ttk.Label(ctrl, text="X").grid(row=2, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(ctrl, textvariable=self.abs_x_var, width=12).grid(row=2, column=1, sticky="w", pady=(8, 0))
        ttk.Button(ctrl, text="Go X", command=lambda: self.on_move_abs("X")).grid(
            row=2, column=2, sticky="w", padx=6, pady=(8, 0)
        )
        ttk.Entry(ctrl, textvariable=self.step_x_var, width=10).grid(row=2, column=3, sticky="w", pady=(8, 0))
        ttk.Button(ctrl, text="-", width=4, command=lambda: self.on_jog("X", -1)).grid(
            row=2, column=4, sticky="w", pady=(8, 0)
        )
        ttk.Button(ctrl, text="+", width=4, command=lambda: self.on_jog("X", +1)).grid(
            row=2, column=5, sticky="w", padx=(6, 0), pady=(8, 0)
        )
        self.slider_x = ttk.Scale(
            ctrl,
            from_=ABS_MIN_MM,
            to=ABS_MAX_MM,
            orient="horizontal",
            variable=self.slider_x_var,
            command=lambda value: self.on_slider_change("X", value),
            length=260,
        )
        self.slider_x.grid(row=2, column=6, sticky="we", padx=(10, 0), pady=(8, 0))
        self.slider_x.bind("<ButtonRelease-1>", lambda _evt: self.on_slider_release("X"))
        self.slider_x.bind("<MouseWheel>", lambda evt: self.on_slider_wheel("X", evt))
        self.slider_x.bind("<Button-4>", lambda evt: self.on_slider_wheel("X", evt))
        self.slider_x.bind("<Button-5>", lambda evt: self.on_slider_wheel("X", evt))
        ttk.Label(ctrl, textvariable=self.slider_x_label_var, width=11).grid(
            row=2, column=7, sticky="w", padx=(8, 0), pady=(8, 0)
        )

        ttk.Label(ctrl, text="Y").grid(row=3, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(ctrl, textvariable=self.abs_y_var, width=12).grid(row=3, column=1, sticky="w", pady=(8, 0))
        ttk.Button(ctrl, text="Go Y", command=lambda: self.on_move_abs("Y")).grid(
            row=3, column=2, sticky="w", padx=6, pady=(8, 0)
        )
        ttk.Entry(ctrl, textvariable=self.step_y_var, width=10).grid(row=3, column=3, sticky="w", pady=(8, 0))
        ttk.Button(ctrl, text="-", width=4, command=lambda: self.on_jog("Y", -1)).grid(
            row=3, column=4, sticky="w", pady=(8, 0)
        )
        ttk.Button(ctrl, text="+", width=4, command=lambda: self.on_jog("Y", +1)).grid(
            row=3, column=5, sticky="w", padx=(6, 0), pady=(8, 0)
        )
        self.slider_y = ttk.Scale(
            ctrl,
            from_=ABS_MIN_MM,
            to=ABS_MAX_MM,
            orient="horizontal",
            variable=self.slider_y_var,
            command=lambda value: self.on_slider_change("Y", value),
            length=260,
        )
        self.slider_y.grid(row=3, column=6, sticky="we", padx=(10, 0), pady=(8, 0))
        self.slider_y.bind("<ButtonRelease-1>", lambda _evt: self.on_slider_release("Y"))
        self.slider_y.bind("<MouseWheel>", lambda evt: self.on_slider_wheel("Y", evt))
        self.slider_y.bind("<Button-4>", lambda evt: self.on_slider_wheel("Y", evt))
        self.slider_y.bind("<Button-5>", lambda evt: self.on_slider_wheel("Y", evt))
        ttk.Label(ctrl, textvariable=self.slider_y_label_var, width=11).grid(
            row=3, column=7, sticky="w", padx=(8, 0), pady=(8, 0)
        )
        ctrl.columnconfigure(6, weight=1)

        status = ttk.LabelFrame(root, text="3) Tableau position moteurs", padding=10)
        status.pack(fill="both", expand=True, pady=(10, 0))

        columns = ("axis", "connected", "port", "position", "state", "error")
        self.position_table = ttk.Treeview(status, columns=columns, show="headings", height=6)
        self.position_table.heading("axis", text="Axe")
        self.position_table.heading("connected", text="Connecte")
        self.position_table.heading("port", text="COM")
        self.position_table.heading("position", text="Position")
        self.position_table.heading("state", text="Etat")
        self.position_table.heading("error", text="Erreur")
        self.position_table.column("axis", width=70, anchor="center")
        self.position_table.column("connected", width=90, anchor="center")
        self.position_table.column("port", width=90, anchor="center")
        self.position_table.column("position", width=130, anchor="e")
        self.position_table.column("state", width=300, anchor="w")
        self.position_table.column("error", width=220, anchor="w")
        self.position_table.grid(row=0, column=0, columnspan=6, sticky="nsew")
        status.rowconfigure(0, weight=1)
        status.columnconfigure(0, weight=1)

        self._table_row_ids = {
            "X": self.position_table.insert("", "end", values=("X", "False", "-", "-", "-", "-")),
            "Y": self.position_table.insert("", "end", values=("Y", "False", "-", "-", "-", "-")),
        }

        ttk.Label(status, text="Status").grid(row=1, column=0, sticky="w", pady=(10, 4))
        self.status_var = tk.StringVar(value="")
        ttk.Label(status, textvariable=self.status_var, foreground="blue").grid(
            row=1, column=1, columnspan=5, sticky="w", pady=(10, 4)
        )

        ttk.Label(status, text="Log").grid(row=2, column=0, sticky="nw")
        self.log_text = tk.Text(status, height=10, width=110)
        self.log_text.grid(row=3, column=0, columnspan=6, sticky="nsew", pady=(4, 0))
        status.rowconfigure(3, weight=1)

    def _set_status(self, text: str):
        if not self._closing:
            self.status_var.set(text)

    def _log(self, message: str):
        if self._closing:
            return
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{timestamp}] {message}\n")
        self.log_text.see("end")

    def _run_async(self, title: str, worker):
        def run():
            try:
                self.after(0, lambda: self._set_status(f"{title}..."))
                worker()
                self.after(0, lambda: self._set_status(f"{title}: OK"))
            except Exception as exc:  # noqa: BLE001
                self.after(0, lambda: self._set_status(f"{title}: ERROR"))
                self.after(0, lambda: self._log(f"{title} failed: {exc}"))
                self.after(0, lambda: self._log(traceback.format_exc()))

        threading.Thread(target=run, daemon=True).start()

    def _get_live_speed(self) -> float:
        speed = float(self.live_speed_mm_s)
        if speed < LIVE_MIN_SPEED_MM_S:
            speed = LIVE_MIN_SPEED_MM_S
        if speed > LIVE_MAX_SPEED_MM_S:
            speed = LIVE_MAX_SPEED_MM_S
        return speed

    def _start_live_commanders(self):
        self._stop_live_commanders()
        if self.axis_x is not None and self.axis_x.connected:
            self._live_commanders["X"] = AxisLiveCommander(
                self.axis_x,
                "X",
                self._get_live_speed,
                lambda msg: self.after(0, lambda m=msg: self._log(m)),
            )
        if self.axis_y is not None and self.axis_y.connected:
            self._live_commanders["Y"] = AxisLiveCommander(
                self.axis_y,
                "Y",
                self._get_live_speed,
                lambda msg: self.after(0, lambda m=msg: self._log(m)),
            )

    def _stop_live_commanders(self):
        commanders = list(self._live_commanders.values())
        self._live_commanders = {}
        for commander in commanders:
            commander.stop()

    def _get_timeout(self) -> float:
        try:
            value = float(self.timeout_var.get())
            if value <= 0:
                raise ValueError
            return value
        except ValueError as exc:
            raise ConexError("Timeout must be a positive number") from exc

    def _get_address(self) -> int:
        try:
            value = int(self.addr_var.get())
            if value <= 0:
                raise ValueError
            return value
        except ValueError as exc:
            raise ConexError("Address must be a positive integer (usually 1)") from exc

    def _axis_or_error(self, name: str):
        axis = self.axis_x if name == "X" else self.axis_y
        if axis is None or not axis.connected:
            raise ConexError(f"Axis {name} is not connected")
        return axis

    def _active_axes(self, mode: str):
        if mode == "X":
            return [self._axis_or_error("X")]
        if mode == "Y":
            return [self._axis_or_error("Y")]
        return [self._axis_or_error("X"), self._axis_or_error("Y")]

    def _scan_devices_impl(self):
        if self.conex_class is None:
            raise ConexError("Load DLL first")
        tmp = self.conex_class()
        raw = tmp.GetDevices()
        devices = [str(dev).strip() for dev in (raw or []) if str(dev).strip()]
        devices = sorted(set(devices))

        def apply_devices():
            self.x_combo["values"] = devices
            self.y_combo["values"] = devices
            if devices and self.x_port_var.get() not in devices:
                self.x_port_var.set(devices[0])
            if len(devices) > 1 and self.y_port_var.get() not in devices:
                self.y_port_var.set(devices[1])
            elif devices and self.y_port_var.get() not in devices:
                self.y_port_var.set(devices[0])
            self._log(f"Detected devices: {devices}")

        self.after(0, apply_devices)

    def on_load_dll(self):
        path = self.dll_path_var.get().strip()

        def worker():
            conex_class, loaded_from = load_conex_class(path)
            self.conex_class = conex_class
            self.loaded_dll = loaded_from
            self.after(0, lambda: self._log(f"DLL loaded from: {loaded_from}"))

        self._run_async("Load DLL", worker)

    def on_scan_devices(self):
        self._run_async("Scan devices", self._scan_devices_impl)

    def on_connect(self):
        x_port = self.x_port_var.get().strip()
        y_port = self.y_port_var.get().strip()
        try:
            address = self._get_address()
        except ConexError as exc:
            messagebox.showerror("Error", str(exc))
            return

        if self.conex_class is None:
            messagebox.showerror("Error", "Load DLL first")
            return
        if not x_port or not y_port:
            messagebox.showerror("Error", "Select COM ports for X and Y")
            return
        if x_port == y_port:
            messagebox.showerror("Error", "X and Y must use different COM ports")
            return

        def worker():
            self._disconnect_impl(stop_before_close=True, silent=True)
            axis_x = ConexAxis("X", self.conex_class, x_port, address=address)
            axis_y = ConexAxis("Y", self.conex_class, y_port, address=address)

            axis_x.open()
            axis_y.open()

            self.axis_x = axis_x
            self.axis_y = axis_y
            self._start_live_commanders()
            self.after(0, lambda: self._log(f"Connected X={x_port}, Y={y_port}, address={address}"))
            self.after(0, self._refresh_status_once)
            self.after(0, self._ensure_status_loop)

        self._run_async("Connect axes", worker)

    def _disconnect_impl(self, stop_before_close=True, silent=False):
        self._stop_live_commanders()
        for axis in [self.axis_x, self.axis_y]:
            if axis is None:
                continue
            if stop_before_close and axis.connected:
                try:
                    axis.stop()
                except Exception:  # noqa: BLE001
                    pass
            try:
                axis.close()
            except Exception as exc:  # noqa: BLE001
                if not silent:
                    self.after(0, lambda msg=str(exc): self._log(f"Disconnect warning: {msg}"))
        self.axis_x = None
        self.axis_y = None
        self.after(0, self._refresh_status_once)

    def on_disconnect(self):
        self._run_async("Disconnect", lambda: self._disconnect_impl(stop_before_close=False))

    def on_stop_all(self):
        def worker():
            for axis_name in ("X", "Y"):
                axis = self.axis_x if axis_name == "X" else self.axis_y
                if axis is not None and axis.connected:
                    axis.stop()
            self.after(0, self._refresh_status_once)

        self._run_async("STOP X+Y", worker)

    def on_initialize(self):
        try:
            timeout_s = max(60.0, self._get_timeout())
        except ConexError as exc:
            messagebox.showerror("Error", str(exc))
            return

        def worker():
            for axis in self._active_axes("XY"):
                self.after(0, lambda ax=axis.axis_name: self._log(f"Initialize {ax}: start OR"))
                final_state = axis.home(timeout_s=timeout_s)
                self.after(
                    0,
                    lambda ax=axis.axis_name, st=final_state: self._log(
                        f"Initialize {ax}: done ({state_to_text(st)})"
                    ),
                )
            self.after(0, self._refresh_status_once)

        self._run_async("Initialize X+Y", worker)

    def on_debug_ts(self):
        def worker():
            results = []
            for axis_name in ("X", "Y"):
                axis = self.axis_x if axis_name == "X" else self.axis_y
                if axis is None or not axis.connected:
                    results.append(f"{axis_name}: not connected")
                    continue
                error_code, state_code = axis.read_state()
                results.append(
                    f"{axis_name}: err={error_code or '-'} state={state_to_text(state_code)}"
                )
            self.after(0, lambda: self._log(" | ".join(results)))
            self.after(0, self._refresh_status_once)

        self._run_async("Debug TS", worker)

    def on_live_speed_change(self, value):
        try:
            speed = float(value)
        except (TypeError, ValueError):
            return
        speed = max(LIVE_MIN_SPEED_MM_S, min(LIVE_MAX_SPEED_MM_S, speed))
        self.live_speed_mm_s = speed
        self.live_speed_label_var.set(f"{speed:.3f} mm/s")

    def _validate_abs_target(self, axis_name: str, value: float):
        if value < ABS_MIN_MM or value > ABS_MAX_MM:
            raise ConexError(
                f"Target {axis_name} must be in [{ABS_MIN_MM:.1f}, {ABS_MAX_MM:.1f}] mm"
            )

    def on_move_abs(self, mode: str):
        try:
            timeout_s = self._get_timeout()
            targets = {}
            if mode in ("X", "XY"):
                targets["X"] = float(self.abs_x_var.get())
                self._validate_abs_target("X", targets["X"])
            if mode in ("Y", "XY"):
                targets["Y"] = float(self.abs_y_var.get())
                self._validate_abs_target("Y", targets["Y"])
        except ValueError:
            messagebox.showerror("Error", "Absolute targets must be numbers")
            return
        except ConexError as exc:
            messagebox.showerror("Error", str(exc))
            return

        self._suspend_slider_callback = True
        try:
            if "X" in targets:
                self.slider_x_var.set(targets["X"])
                self.slider_x_label_var.set(f"{targets['X']:.3f} mm")
            if "Y" in targets:
                self.slider_y_var.set(targets["Y"])
                self.slider_y_label_var.set(f"{targets['Y']:.3f} mm")
        finally:
            self._suspend_slider_callback = False

        def worker():
            axes = {"X": self._axis_or_error("X"), "Y": self._axis_or_error("Y")}
            for key in targets:
                axes[key].move_absolute(targets[key], timeout_s=timeout_s)
            self.after(0, self._refresh_status_once)

        self._run_async(f"Move abs {mode}", worker)

    def on_slider_change(self, axis_name: str, value):
        try:
            pos = float(value)
        except (TypeError, ValueError):
            return
        pos = max(ABS_MIN_MM, min(ABS_MAX_MM, pos))
        if axis_name == "X":
            self.abs_x_var.set(f"{pos:.3f}")
            self.slider_x_label_var.set(f"{pos:.3f} mm")
        else:
            self.abs_y_var.set(f"{pos:.3f}")
            self.slider_y_label_var.set(f"{pos:.3f} mm")
        if self._suspend_slider_callback:
            return
        commander = self._live_commanders.get(axis_name)
        if commander is not None:
            commander.set_target(pos)

    def on_slider_release(self, axis_name: str):
        commander = self._live_commanders.get(axis_name)
        axis = self.axis_x if axis_name == "X" else self.axis_y
        if axis is None or not axis.connected:
            self._log(f"Slider {axis_name}: axis not connected, no move sent")
            return
        if commander is None:
            self.on_move_abs(axis_name)
            return
        # Force a final target set at release.
        try:
            final_pos = float(self.slider_x_var.get() if axis_name == "X" else self.slider_y_var.get())
            final_pos = max(ABS_MIN_MM, min(ABS_MAX_MM, final_pos))
            commander.set_target(final_pos)
        except (TypeError, ValueError):
            pass

    def on_slider_wheel(self, axis_name: str, event):
        # Trackpad two-finger vertical scroll is mapped to mouse wheel events on Windows.
        if axis_name == "X":
            current = float(self.slider_x_var.get())
        else:
            current = float(self.slider_y_var.get())

        delta = getattr(event, "delta", 0)
        if delta > 0:
            direction = -1
        elif delta < 0:
            direction = 1
        else:
            num = getattr(event, "num", 0)
            direction = -1 if num == 4 else 1 if num == 5 else 0

        if direction == 0:
            return "break"

        state = int(getattr(event, "state", 0))
        if state & 0x0001:  # Shift
            step = WHEEL_STEP_FINE_MM
        elif state & 0x0004:  # Ctrl
            step = WHEEL_STEP_FAST_MM
        else:
            step = WHEEL_STEP_MM

        new_pos = current + (direction * step)
        new_pos = max(ABS_MIN_MM, min(ABS_MAX_MM, new_pos))
        self.on_slider_change(axis_name, new_pos)
        if axis_name == "X":
            self.slider_x_var.set(new_pos)
        else:
            self.slider_y_var.set(new_pos)
        return "break"

    def on_jog(self, axis_name: str, direction: int):
        try:
            timeout_s = self._get_timeout()
            step_raw = self.step_x_var.get() if axis_name == "X" else self.step_y_var.get()
            step = float(step_raw)
            if step <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("Error", f"Step {axis_name} must be a positive number")
            return
        except ConexError as exc:
            messagebox.showerror("Error", str(exc))
            return

        delta = step if direction >= 0 else -step

        def worker():
            axis = self._axis_or_error(axis_name)
            axis.move_relative(delta, timeout_s=timeout_s)
            self.after(0, self._refresh_status_once)

        sign = "+" if direction >= 0 else "-"
        self._run_async(f"Jog {axis_name} {sign}", worker)

    def _apply_snapshot_to_ui(self, axis_name: str, snap: AxisSnapshot):
        connected_text = "True" if snap.connected else "False"
        port_text = snap.port or "-"
        pos_text = "-" if snap.position is None else f"{snap.position:.6f}"
        state_text = "-" if not snap.state_code else state_to_text(snap.state_code)
        err_text = "-" if not snap.error_code else snap.error_code
        if snap.issue:
            err_text = f"{err_text} ({snap.issue})" if err_text != "-" else snap.issue

        row_id = self._table_row_ids.get(axis_name)
        if row_id:
            self.position_table.item(
                row_id,
                values=(axis_name, connected_text, port_text, pos_text, state_text, err_text),
            )

    def _refresh_status_once(self):
        snaps = []
        for axis_name, axis in [("X", self.axis_x), ("Y", self.axis_y)]:
            if axis is None:
                snaps.append((axis_name, AxisSnapshot(connected=False)))
                continue
            snaps.append((axis_name, axis.snapshot()))

        for axis_name, snap in snaps:
            self._apply_snapshot_to_ui(axis_name, snap)

    def _ensure_status_loop(self):
        if self._status_job is None:
            self._status_loop()

    def _status_loop(self):
        if self._closing:
            self._status_job = None
            return
        try:
            self._refresh_status_once()
        except Exception as exc:  # noqa: BLE001
            self._log(f"Status refresh error: {exc}")
        self._status_job = self.after(900, self._status_loop)

    def _on_close(self):
        self._closing = True
        if self._status_job is not None:
            self.after_cancel(self._status_job)
            self._status_job = None
        try:
            self._stop_live_commanders()
            self._disconnect_impl(stop_before_close=True, silent=True)
        finally:
            self.destroy()


def main():
    app = ConexTestApp()
    app.mainloop()


if __name__ == "__main__":
    main()
