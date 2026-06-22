import numpy as np
from imgui_bundle import imgui, implot

from bob_diagnostics.state.app_state import AppState

_SCROLL_WINDOW = 10.0  # seconds


def _scroll_limits(t_arr: np.ndarray) -> tuple[float, float]:
    t_max = float(t_arr[-1])
    return max(0.0, t_max - _SCROLL_WINDOW), t_max


def gui_pid_output(app_state: AppState) -> None:
    buf = app_state.buffer

    if not buf:
        imgui.text_disabled("Waiting for telemetry...")
        return

    arrs = buf.arrays()
    t   = arrs["t"]
    t_min, t_max = _scroll_limits(t)
    avail = imgui.get_content_region_avail()

    # ---- PID controller output ----------------------------------------------
    pid_h = avail.y * 0.52
    if implot.begin_plot("PID Output", imgui.ImVec2(avail.x, pid_h)):
        implot.setup_axes("time (s)", "output (raw)")
        implot.setup_axis_limits(implot.ImAxis_.x1, t_min, t_max, implot.Cond_.always)
        implot.setup_axis_limits(implot.ImAxis_.y1, -1100.0, 1100.0, implot.Cond_.once)

        # Zero reference
        zero_x = np.array([t_min, t_max], dtype=np.float32)
        implot.plot_line("##zero", zero_x, np.zeros(2, dtype=np.float32))

        implot.plot_line("pid_out", t, arrs["pid_output"])
        implot.end_plot()

    imgui.spacing()

    # ---- Motor PWM ----------------------------------------------------------
    remaining = imgui.get_content_region_avail()
    if implot.begin_plot("Motor PWM", imgui.ImVec2(avail.x, remaining.y)):
        implot.setup_axes("time (s)", "PWM (0–1023)")
        implot.setup_axis_limits(implot.ImAxis_.x1, t_min, t_max, implot.Cond_.always)
        implot.setup_axis_limits(implot.ImAxis_.y1, 0.0, 1100.0, implot.Cond_.once)
        implot.plot_line("motor", t, arrs["motor"])
        implot.end_plot()

    # ---- Live metrics -------------------------------------------------------
    last_pid   = float(arrs["pid_output"][-1])
    last_motor = int(arrs["motor"][-1])
    imgui.text(f"pid_out = {last_pid:+.1f}   motor = {last_motor}")
