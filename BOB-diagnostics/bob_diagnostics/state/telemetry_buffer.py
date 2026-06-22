from collections import deque

import numpy as np

BUFFER_SIZE = 600  # 12 s @ 50 Hz


class TelemetryBuffer:
    """Ring buffer of telemetry frames received over WebSocket.

    All deques share the same maxlen so array lengths are always equal.
    Only written from the GUI thread (via AppState.poll()), so no locking needed.
    """

    def __init__(self, maxlen: int = BUFFER_SIZE):
        self._maxlen = maxlen
        self._t0: float = 0.0
        self._ready = False
        self.frame_count: int = 0

        self.t          = deque(maxlen=maxlen)  # seconds, relative to first frame
        self.roll       = deque(maxlen=maxlen)  # degrees
        self.pitch      = deque(maxlen=maxlen)  # degrees
        self.accel_x    = deque(maxlen=maxlen)  # m/s²
        self.accel_y    = deque(maxlen=maxlen)
        self.accel_z    = deque(maxlen=maxlen)
        self.gyro_x     = deque(maxlen=maxlen)  # °/s
        self.gyro_y     = deque(maxlen=maxlen)
        self.gyro_z     = deque(maxlen=maxlen)
        self.temp_c     = deque(maxlen=maxlen)  # °C
        self.pid_output = deque(maxlen=maxlen)  # signed, ±1023
        self.motor      = deque(maxlen=maxlen)  # PWM magnitude 0–1023

    def push(self, frame: dict) -> None:
        t_s = frame["t"] / 1_000_000.0
        if not self._ready:
            self._t0 = t_s
            self._ready = True

        self.t.append(t_s - self._t0)
        self.roll.append(frame.get("roll", 0.0))
        self.pitch.append(frame.get("pitch", 0.0))
        self.accel_x.append(frame.get("ax", 0.0))
        self.accel_y.append(frame.get("ay", 0.0))
        self.accel_z.append(frame.get("az", 0.0))
        self.gyro_x.append(frame.get("gx", 0.0))
        self.gyro_y.append(frame.get("gy", 0.0))
        self.gyro_z.append(frame.get("gz", 0.0))
        self.temp_c.append(frame.get("temp", 0.0))
        self.pid_output.append(frame.get("pid_out", 0.0))
        self.motor.append(float(frame.get("motor", 0)))
        self.frame_count += 1

    def arrays(self) -> dict[str, np.ndarray]:
        """Snapshot all deques as float32 numpy arrays for implot."""
        keys = (
            "t", "roll", "pitch",
            "accel_x", "accel_y", "accel_z",
            "gyro_x",  "gyro_y",  "gyro_z",
            "temp_c", "pid_output", "motor",
        )
        return {k: np.array(getattr(self, k), dtype=np.float32) for k in keys}

    def clear(self) -> None:
        for attr in ("t", "roll", "pitch", "accel_x", "accel_y", "accel_z",
                     "gyro_x", "gyro_y", "gyro_z", "temp_c", "pid_output", "motor"):
            getattr(self, attr).clear()
        self._ready = False
        self.frame_count = 0

    def __len__(self) -> int:
        return len(self.t)

    def __bool__(self) -> bool:
        return bool(self.t)
