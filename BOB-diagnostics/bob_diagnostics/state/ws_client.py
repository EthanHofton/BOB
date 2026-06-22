import json
import queue
import threading
import time

import websocket


class WsClient:
    """Background-thread WebSocket receiver.

    Connects to BOB's telemetry stream (ws://<host>/telemetry).
    Parsed JSON frames are placed on `inbox`; AppState.poll() drains it
    into the TelemetryBuffer each frame.

    Auto-reconnects every 2 s on disconnect while `_running` is True.
    """

    RECONNECT_DELAY = 2.0

    def __init__(self) -> None:
        self.inbox: queue.Queue[dict] = queue.Queue(maxsize=300)
        self.connected: bool = False
        self.frame_count: int = 0
        self.last_error: str = ""

        self._url: str = ""
        self._running: bool = False
        self._ws: websocket.WebSocketApp | None = None
        self._thread: threading.Thread | None = None

    # ------------------------------------------------------------------

    def connect(self, host: str, path: str = "/telemetry", port: int = 80) -> None:
        if self._running:
            self.disconnect()
        self._url = f"ws://{host}:{port}{path}"
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def disconnect(self) -> None:
        self._running = False
        if self._ws:
            self._ws.close()
        self.connected = False

    # ------------------------------------------------------------------

    def _run(self) -> None:
        while self._running:
            self._ws = websocket.WebSocketApp(
                self._url,
                on_open=self._on_open,
                on_message=self._on_message,
                on_error=self._on_error,
                on_close=self._on_close,
            )
            self._ws.run_forever()
            if self._running:
                time.sleep(self.RECONNECT_DELAY)

    def _on_open(self, ws: websocket.WebSocketApp) -> None:
        self.connected = True
        self.last_error = ""

    def _on_message(self, ws: websocket.WebSocketApp, message: str) -> None:
        try:
            frame = json.loads(message)
        except json.JSONDecodeError:
            return
        self.frame_count += 1
        if self.inbox.full():
            try:
                self.inbox.get_nowait()  # drop oldest if GUI is lagging
            except queue.Empty:
                pass
        self.inbox.put_nowait(frame)

    def _on_error(self, ws: websocket.WebSocketApp, error: Exception) -> None:
        self.connected = False
        self.last_error = str(error)

    def _on_close(self, ws: websocket.WebSocketApp, code: int, msg: str) -> None:
        self.connected = False
