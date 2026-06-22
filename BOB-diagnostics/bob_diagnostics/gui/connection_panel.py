from imgui_bundle import imgui

from bob_diagnostics.state.app_state import AppState

_WS_PORT   = 80
_WS_PATH   = "/telemetry"
_TCP_PORT  = 4242


def _indicator(connected: bool) -> None:
    """Draws a small filled circle — green when connected, red when not."""
    draw = imgui.get_window_draw_list()
    pos  = imgui.get_cursor_screen_pos()
    r    = 5.0
    cx   = pos.x + r
    cy   = pos.y + imgui.get_text_line_height() * 0.5
    col  = imgui.get_color_u32(
        imgui.ImVec4(0.2, 0.9, 0.3, 1.0) if connected else imgui.ImVec4(0.9, 0.2, 0.2, 1.0)
    )
    draw.add_circle_filled(imgui.ImVec2(cx, cy), r, col)
    imgui.dummy(imgui.ImVec2(r * 2 + 6, imgui.get_text_line_height()))


def gui_connection(app_state: AppState) -> None:
    ws  = app_state.ws
    tcp = app_state.tcp

    # ---- Host input ---------------------------------------------------------
    imgui.text("BOB host")
    imgui.same_line()
    imgui.set_next_item_width(160)
    changed, app_state.bob_host = imgui.input_text("##host", app_state.bob_host)

    imgui.spacing()
    imgui.separator()
    imgui.spacing()

    # ---- WebSocket ----------------------------------------------------------
    _indicator(ws.connected)
    imgui.same_line()
    imgui.text("WebSocket")
    imgui.same_line(spacing=12)
    imgui.text_disabled(f"ws://{app_state.bob_host}:{_WS_PORT}{_WS_PATH}")
    imgui.same_line(spacing=12)

    if ws.connected:
        if imgui.button("Disconnect##ws"):
            ws.disconnect()
            app_state.buffer.clear()
    else:
        if imgui.button("Connect##ws"):
            ws.connect(app_state.bob_host, _WS_PATH, _WS_PORT)

    if ws.last_error:
        imgui.same_line(spacing=8)
        imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(1.0, 0.4, 0.4, 1.0))
        imgui.text(ws.last_error)
        imgui.pop_style_color()

    imgui.spacing()

    # ---- TCP ----------------------------------------------------------------
    _indicator(tcp.connected)
    imgui.same_line()
    imgui.text("TCP tuning ")
    imgui.same_line(spacing=12)
    imgui.text_disabled(f"{app_state.bob_host}:{_TCP_PORT}")
    imgui.same_line(spacing=12)

    if tcp.connected:
        if imgui.button("Disconnect##tcp"):
            tcp.disconnect()
    else:
        if imgui.button("Connect##tcp"):
            tcp.connect(app_state.bob_host, _TCP_PORT)

    if tcp.last_error:
        imgui.same_line(spacing=8)
        imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(1.0, 0.4, 0.4, 1.0))
        imgui.text(tcp.last_error)
        imgui.pop_style_color()

    # ---- Stats --------------------------------------------------------------
    imgui.spacing()
    imgui.separator()
    imgui.spacing()

    imgui.text_disabled(
        f"WS frames: {ws.frame_count}   "
        f"buffer: {len(app_state.buffer)}   "
        f"FPS: {imgui.get_io().framerate:.0f}"
    )
