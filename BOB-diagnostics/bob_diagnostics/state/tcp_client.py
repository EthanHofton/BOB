import json
import socket
import threading
from typing import Any


class TcpClient:
    """Persistent TCP connection to BOB's tuning server (port 4242).

    Commands are fire-and-forget from the GUI thread: `send_command` spawns a
    short-lived daemon thread, sends the JSON command, reads the reply, and
    stores the result in `last_response` / `last_error` for the GUI to display.

    A single mutex serialises concurrent commands so responses are never mixed.
    """

    RECV_TIMEOUT = 3.0

    def __init__(self) -> None:
        self._sock: socket.socket | None = None
        self._lock = threading.Lock()
        self.connected: bool = False
        self.last_response: dict[str, Any] | None = None
        self.last_error: str = ""

    # ------------------------------------------------------------------

    def connect(self, host: str, port: int = 4242) -> bool:
        with self._lock:
            self._close_socket()
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(5.0)
                s.connect((host, port))
                s.settimeout(self.RECV_TIMEOUT)
                self._sock = s
                self.connected = True
                self.last_error = ""
                return True
            except OSError as e:
                self.last_error = str(e)
                self.connected = False
                return False

    def disconnect(self) -> None:
        with self._lock:
            self._close_socket()

    def _close_socket(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        self.connected = False

    # ------------------------------------------------------------------

    def send_command(self, cmd: dict) -> None:
        """Non-blocking: sends cmd in a daemon thread, result in last_response."""
        threading.Thread(target=self._send_recv, args=(cmd,), daemon=True).start()

    def _send_recv(self, cmd: dict) -> None:
        with self._lock:
            if not self._sock:
                self.last_error = "Not connected"
                return
            try:
                payload = (json.dumps(cmd) + "\n").encode()
                self._sock.sendall(payload)

                buf = b""
                while b"\n" not in buf:
                    chunk = self._sock.recv(512)
                    if not chunk:
                        raise ConnectionError("Connection closed by server")
                    buf += chunk

                line = buf.decode().split("\n")[0].strip()
                self.last_response = json.loads(line)
                self.last_error = ""
            except OSError as e:
                self.last_error = str(e)
                self.last_response = None
                self.connected = False
                self._close_socket()
            except json.JSONDecodeError as e:
                self.last_error = f"Bad JSON response: {e}"
                self.last_response = None
