import numpy as np
from imgui_bundle import imgui, implot

from bob_diagnostics.state.app_state import AppState

_FALL_THRESHOLD = 45.0   # degrees — mirrors BALANCE_FALL_THRESHOLD_DEG
_SCROLL_WINDOW  = 10.0   # seconds of history shown in plots


def _scroll_limits(t_arr: np.ndarray) -> tuple[float, float]:
    t_max = float(t_arr[-1])
    return max(0.0, t_max - _SCROLL_WINDOW), t_max


def gui_imu(app_state: AppState) -> None:
    buf = app_state.buffer

    if not buf:
        imgui.text_disabled("Waiting for telemetry...")
        return

    arrs = buf.arrays()
    t   = arrs["t"]
    t_min, t_max = _scroll_limits(t)
    avail = imgui.get_content_region_avail()

    # ---- Roll / Pitch -------------------------------------------------------
    roll_plot_h = avail.y * 0.42
    if implot.begin_plot("Roll / Pitch", imgui.ImVec2(avail.x, roll_plot_h)):
        implot.setup_axes("time (s)", "angle (°)")
        implot.setup_axis_limits(implot.ImAxis_.x1, t_min, t_max, implot.Cond_.always)
        implot.setup_axis_limits(implot.ImAxis_.y1, -90.0, 90.0, implot.Cond_.once)

        # Reference lines at fall threshold
        thresh_x = np.array([t_min, t_max], dtype=np.float32)
        implot.plot_line("±45° fall", thresh_x, np.full(2, _FALL_THRESHOLD,  dtype=np.float32))
        implot.plot_line("##th-",     thresh_x, np.full(2, -_FALL_THRESHOLD, dtype=np.float32))

        implot.plot_line("roll",  t, arrs["roll"])
        implot.plot_line("pitch", t, arrs["pitch"])
        implot.end_plot()

    # Current roll value with colour indicating fall state
    last_roll = float(arrs["roll"][-1])
    if abs(last_roll) >= _FALL_THRESHOLD:
        imgui.text_colored(imgui.ImVec4(1.0, 0.25, 0.25, 1.0), f"FALLEN  roll = {last_roll:+.2f}°")
    else:
        imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), f"roll = {last_roll:+.2f}°")

    imgui.same_line(spacing=24)
    last_pitch = float(arrs["pitch"][-1])
    imgui.text(f"pitch = {last_pitch:+.2f}°")

    imgui.same_line(spacing=24)
    imgui.text_disabled(f"temp = {float(arrs['temp_c'][-1]):.1f} °C")

    imgui.spacing()

    # ---- Accel / Gyro tab bar -----------------------------------------------
    remaining = imgui.get_content_region_avail()
    if imgui.begin_tab_bar("##imu_tabs"):

        if imgui.begin_tab_item("Accelerometer")[0]:
            if implot.begin_plot("Accel", imgui.ImVec2(remaining.x, remaining.y - 30)):
                implot.setup_axes("time (s)", "m/s²")
                implot.setup_axis_limits(implot.ImAxis_.x1, t_min, t_max, implot.Cond_.always)
                implot.setup_axis_limits(implot.ImAxis_.y1, -20.0, 20.0, implot.Cond_.once)
                implot.plot_line("ax", t, arrs["accel_x"])
                implot.plot_line("ay", t, arrs["accel_y"])
                implot.plot_line("az", t, arrs["accel_z"])
                implot.end_plot()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Gyroscope")[0]:
            if implot.begin_plot("Gyro", imgui.ImVec2(remaining.x, remaining.y - 30)):
                implot.setup_axes("time (s)", "°/s")
                implot.setup_axis_limits(implot.ImAxis_.x1, t_min, t_max, implot.Cond_.always)
                implot.setup_axis_limits(implot.ImAxis_.y1, -600.0, 600.0, implot.Cond_.once)
                implot.plot_line("gx", t, arrs["gyro_x"])
                implot.plot_line("gy", t, arrs["gyro_y"])
                implot.plot_line("gz", t, arrs["gyro_z"])
                implot.end_plot()
            imgui.end_tab_item()

        imgui.end_tab_bar()
