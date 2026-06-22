from imgui_bundle import imgui

from bob_diagnostics.state.app_state import AppState


def show_status_bar(app_state: AppState) -> None:
    ws  = app_state.ws
    tcp = app_state.tcp

    # WS indicator
    ws_col = imgui.ImVec4(0.2, 0.9, 0.3, 1.0) if ws.connected  else imgui.ImVec4(0.8, 0.3, 0.3, 1.0)
    imgui.push_style_color(imgui.Col_.text, ws_col)
    imgui.text("WS")
    imgui.pop_style_color()

    imgui.same_line(spacing=4)
    imgui.text_disabled("|")
    imgui.same_line(spacing=4)

    # TCP indicator
    tcp_col = imgui.ImVec4(0.2, 0.9, 0.3, 1.0) if tcp.connected else imgui.ImVec4(0.8, 0.3, 0.3, 1.0)
    imgui.push_style_color(imgui.Col_.text, tcp_col)
    imgui.text("TCP")
    imgui.pop_style_color()

    imgui.same_line(spacing=12)
    imgui.text_disabled(f"frames: {ws.frame_count}")

    if app_state.buffer:
        arrs = app_state.buffer.arrays()
        last_roll = float(arrs["roll"][-1])
        imgui.same_line(spacing=12)
        if abs(last_roll) >= 45.0:
            imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(1.0, 0.3, 0.3, 1.0))
            imgui.text(f"FALLEN  roll={last_roll:+.1f}°")
            imgui.pop_style_color()
        else:
            imgui.text_disabled(f"roll={last_roll:+.1f}°")

    imgui.same_line(spacing=12)
    imgui.text_disabled(f"FPS: {imgui.get_io().framerate:.0f}")
