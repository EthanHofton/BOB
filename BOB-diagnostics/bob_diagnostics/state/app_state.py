import queue
from dataclasses import dataclass, field
from typing import Optional

from bob_diagnostics.state.tcp_client import TcpClient
from bob_diagnostics.state.telemetry_buffer import TelemetryBuffer
from bob_diagnostics.state.ws_client import WsClient

# Defaults mirror the firmware constants in balance.h
_DEFAULT_KP = 100.92
_DEFAULT_KI = 0.5
_DEFAULT_KD = 1.2


@dataclass
class AppState:
    ws: WsClient
    tcp: TcpClient

    buffer: TelemetryBuffer = field(default_factory=TelemetryBuffer)

    # Connection config (edited in the connection panel)
    bob_host: str = "192.168.4.1"

    # PID tuning panel — values currently in the edit fields
    tune_kp:              float = _DEFAULT_KP
    tune_ki:              float = _DEFAULT_KI
    tune_kd:              float = _DEFAULT_KD
    tune_setpoint:        float = 0.0
    tune_coast_threshold: float = 0.10

    # Last values confirmed read from the firmware (None = never fetched)
    firmware_kp:             Optional[float] = None
    firmware_ki:             Optional[float] = None
    firmware_kd:             Optional[float] = None
    firmware_setpoint:       Optional[float] = None
    firmware_coast_threshold: Optional[float] = None

    # Set to True when get_params is sent; poll() clears it once the response
    # arrives so we can auto-load without a second button click.
    _pending_params: bool = field(default=False, repr=False)

    # Motor control panel
    motor_manual:      bool  = False
    motor_drive_mode:  str   = "raw"  # "raw" | "speed"
    motor_raw_value:   float = 0.0    # -1.0 to +1.0, no deadpoint remap
    motor_speed_value: float = 0.0    # -1.0 to +1.0, deadpoint remap applied
    motor_deadpoint:   float = 0.643  # mirrors MOTOR_DEADPOINT_DEFAULT

    def poll(self) -> None:
        """Called every frame (pre_new_frame). Drains WS inbox and resolves
        any pending get_params response."""
        if not self.tcp.connected and self.motor_manual:
            self.motor_manual = False

        if self._pending_params:
            if self._try_load_params():
                self._pending_params = False

        while True:
            try:
                frame = self.ws.inbox.get_nowait()
                self.buffer.push(frame)
            except queue.Empty:
                break

    def _try_load_params(self) -> bool:
        resp = self.tcp.last_response
        if not resp or resp.get("status") != "ok" or "params" not in resp:
            return False
        p = resp["params"]
        self.firmware_kp              = float(p.get("kp",       0.0))
        self.firmware_ki              = float(p.get("ki",       0.0))
        self.firmware_kd              = float(p.get("kd",       0.0))
        self.firmware_setpoint        = float(p.get("setpoint", 0.0))
        self.firmware_coast_threshold = float(p.get("coast",    0.0))
        return True

    # ------------------------------------------------------------------

    def cmd_set_gains(self) -> None:
        self.tcp.send_command({
            "cmd": "set_gains",
            "kp": round(float(self.tune_kp), 6),
            "ki": round(float(self.tune_ki), 6),
            "kd": round(float(self.tune_kd), 6),
        })

    def cmd_set_setpoint(self) -> None:
        self.tcp.send_command({
            "cmd": "set_setpoint",
            "value": round(float(self.tune_setpoint), 6),
        })

    def cmd_reset_pid(self) -> None:
        self.tcp.send_command({"cmd": "reset_pid"})

    def cmd_get_params(self) -> None:
        self._pending_params = True
        self.tcp.send_command({"cmd": "get_params"})

    def cmd_set_coast(self) -> None:
        self.tcp.send_command({"cmd": "set_coast", "value": round(float(self.tune_coast_threshold), 4)})

    def cmd_load_params_to_fields(self) -> None:
        """Copy last firmware read into the editable fields."""
        if self.firmware_kp is not None:
            self.tune_kp              = self.firmware_kp
            self.tune_ki              = self.firmware_ki
            self.tune_kd              = self.firmware_kd
            self.tune_setpoint        = self.firmware_setpoint
            if self.firmware_coast_threshold is not None:
                self.tune_coast_threshold = self.firmware_coast_threshold

    # kept for backwards compatibility
    def apply_get_params_response(self) -> bool:
        self.cmd_load_params_to_fields()
        return self.firmware_kp is not None

    def cmd_motor_manual(self) -> None:
        self.tcp.send_command({"cmd": "motor_manual", "enabled": self.motor_manual})

    def cmd_motor_raw(self) -> None:
        self.tcp.send_command({"cmd": "motor_raw", "value": round(float(self.motor_raw_value), 4)})

    def cmd_motor_speed(self) -> None:
        self.tcp.send_command({"cmd": "motor_speed", "value": round(float(self.motor_speed_value), 4)})

    def cmd_motor_coast(self) -> None:
        self.tcp.send_command({"cmd": "motor_coast"})

    def cmd_set_deadpoint(self) -> None:
        self.tcp.send_command({"cmd": "set_deadpoint", "value": round(float(self.motor_deadpoint), 4)})

    def cmd_get_deadpoint(self) -> None:
        self.tcp.send_command({"cmd": "get_deadpoint"})

    def apply_get_deadpoint_response(self) -> bool:
        resp = self.tcp.last_response
        if not resp or resp.get("status") != "ok" or "deadpoint" not in resp:
            return False
        self.motor_deadpoint = float(resp["deadpoint"])
        return True
