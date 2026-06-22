from imgui_bundle import imgui

from bob_diagnostics.state.app_state import AppState


def gui_motor_control(app_state: AppState) -> None:
    tcp = app_state.tcp
    tcp_ok = tcp.connected

    # ---- Manual mode toggle -------------------------------------------------
    if not tcp_ok:
        imgui.begin_disabled()

    changed, new_val = imgui.checkbox(
        "Manual Mode  (suspends balance PID)", app_state.motor_manual
    )
    if changed and tcp_ok:
        app_state.motor_manual = new_val
        app_state.cmd_motor_manual()

    if not tcp_ok:
        imgui.end_disabled()

    if app_state.motor_manual:
        imgui.same_line(spacing=20)
        imgui.text_colored(imgui.ImVec4(1.0, 0.45, 0.0, 1.0), "WARNING: BALANCE DISABLED")
    elif not tcp_ok:
        imgui.same_line(spacing=16)
        imgui.text_disabled("(connect TCP first)")

    imgui.separator()
    imgui.spacing()

    # ---- Drive mode selector ------------------------------------------------
    imgui.text("Drive mode:")
    imgui.same_line()
    if imgui.radio_button("Raw PID  (±1023)", app_state.motor_drive_mode == "raw"):
        app_state.motor_drive_mode = "raw"
    imgui.same_line(spacing=20)
    if imgui.radio_button("Normalised speed  (±1.0)", app_state.motor_drive_mode == "speed"):
        app_state.motor_drive_mode = "speed"

    imgui.spacing()

    # ---- Value slider + drive controls --------------------------------------
    can_drive = app_state.motor_manual and tcp_ok
    if not can_drive:
        imgui.begin_disabled()

    avail_w = imgui.get_content_region_avail().x

    if app_state.motor_drive_mode == "raw":
        imgui.set_next_item_width(avail_w - 10)
        _, app_state.motor_raw_value = imgui.slider_float(
            "##raw", app_state.motor_raw_value, -1.0, 1.0, "%.4f"
        )
        imgui.text("Raw fraction  (no deadpoint remap — for calibration)")
    else:
        imgui.set_next_item_width(avail_w - 10)
        _, app_state.motor_speed_value = imgui.slider_float(
            "##spd", app_state.motor_speed_value, -1.0, 1.0, "%.3f"
        )
        imgui.text("Speed  (−1.0 = full reverse, +1.0 = full forward)")

    imgui.spacing()

    btn_w = (avail_w - 12) / 2
    if imgui.button("Drive", imgui.ImVec2(btn_w, 0)):
        if app_state.motor_drive_mode == "raw":
            app_state.cmd_motor_raw()
        else:
            app_state.cmd_motor_speed()
    imgui.same_line(spacing=12)
    if imgui.button("Coast", imgui.ImVec2(btn_w, 0)):
        app_state.cmd_motor_coast()

    if not can_drive:
        imgui.end_disabled()

    imgui.spacing()
    imgui.separator()
    imgui.spacing()

    # ---- Deadpoint calibration ----------------------------------------------
    imgui.text("Deadpoint")
    imgui.set_next_item_width(avail_w - 140)
    _, app_state.motor_deadpoint = imgui.slider_float(
        "##dp", app_state.motor_deadpoint, 0.0, 0.99, "%.4f"
    )
    imgui.same_line()
    if not tcp_ok:
        imgui.begin_disabled()
    if imgui.button("Apply##dp", imgui.ImVec2(60, 0)):
        app_state.cmd_set_deadpoint()
    imgui.same_line()
    if imgui.button("Read##dp", imgui.ImVec2(60, 0)):
        app_state.cmd_get_deadpoint()
        app_state.apply_get_deadpoint_response()
    if not tcp_ok:
        imgui.end_disabled()

    imgui.spacing()
    imgui.separator()
    imgui.spacing()

    # ---- Last TCP response / error ------------------------------------------
    if tcp.last_error:
        imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(1.0, 0.35, 0.35, 1.0))
        imgui.text_wrapped(f"Error: {tcp.last_error}")
        imgui.pop_style_color()
    elif tcp.last_response:
        resp = tcp.last_response
        status = resp.get("status", "?")
        if status == "ok":
            imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(0.4, 0.9, 0.4, 1.0))
            manual_str = f"  manual={resp['manual']}" if "manual" in resp else ""
            imgui.text(f"OK{manual_str}")
            imgui.pop_style_color()
        else:
            imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(1.0, 0.6, 0.2, 1.0))
            imgui.text_wrapped(f"firmware: {resp.get('message', str(resp))}")
            imgui.pop_style_color()

    imgui.spacing()
    imgui.separator()
    imgui.spacing()

    # ---- Live motor telemetry -----------------------------------------------
    buf = app_state.buffer
    if not buf:
        imgui.text_disabled("Waiting for telemetry...")
        return

    arrs = buf.arrays()
    last_motor = int(arrs["motor"][-1])
    last_pid   = float(arrs["pid_output"][-1])

    frac = last_motor / 1023.0
    imgui.text(f"Motor PWM:  {last_motor:4d} / 1023")
    imgui.same_line(spacing=16)
    imgui.text_disabled(f"({frac * 100:.1f} %)")
    imgui.progress_bar(frac, imgui.ImVec2(avail_w, 0), "")

    imgui.spacing()
    imgui.text(f"PID output: {last_pid:+.1f}")

    if last_motor == 0:
        imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), "  coasting")
    elif last_motor < int(1023 * 0.30):
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.0, 1.0), "  below typical stall zone")
    else:
        imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), "  driving")
