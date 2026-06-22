from imgui_bundle import imgui

from bob_diagnostics.state.app_state import AppState

_GAINS = [
    ("Kp",       "tune_kp",              "firmware_kp",              0.05,   0.0, 500.0),
    ("Ki",       "tune_ki",              "firmware_ki",              0.002,  0.0,  20.0),
    ("Kd",       "tune_kd",              "firmware_kd",              0.002,  0.0,  20.0),
    ("Setpoint", "tune_setpoint",        "firmware_setpoint",        0.05,  -30.0, 30.0),
    ("Coast",    "tune_coast_threshold", "firmware_coast_threshold", 0.002,  0.0,   1.0),
]

_MISMATCH_COL  = imgui.ImVec4(1.0, 0.75, 0.2, 1.0)  # amber — edit ≠ firmware
_MATCH_COL     = imgui.ImVec4(0.4, 0.9, 0.4, 1.0)   # green — in sync
_UNKNOWN_COL   = imgui.ImVec4(0.55, 0.55, 0.55, 1.0) # grey  — never read


def gui_pid_tuning(app_state: AppState) -> None:
    tcp = app_state.tcp

    imgui.push_item_width(-1)

    # ---- Gain table ---------------------------------------------------------
    flags = imgui.TableFlags_.sizing_fixed_fit | imgui.TableFlags_.no_pad_outer_x
    if imgui.begin_table("##gains", 4, flags):
        imgui.table_setup_column("##label",    imgui.TableColumnFlags_.width_fixed,   68.0)
        imgui.table_setup_column("##drag",     imgui.TableColumnFlags_.width_stretch)
        imgui.table_setup_column("##fw_label", imgui.TableColumnFlags_.width_fixed,   50.0)
        imgui.table_setup_column("##fw_val",   imgui.TableColumnFlags_.width_fixed,   80.0)

        # Header row
        imgui.table_next_row()
        imgui.table_set_column_index(2)
        imgui.text_disabled("on BOB")

        for label, attr, fw_attr, speed, v_min, v_max in _GAINS:
            imgui.table_next_row()

            imgui.table_next_column()
            imgui.text(label)

            imgui.table_next_column()
            imgui.set_next_item_width(-1)
            changed, new_val = imgui.drag_float(
                f"##{attr}", getattr(app_state, attr),
                v_speed=speed, v_min=v_min, v_max=v_max, format="%.4f",
            )
            if changed:
                setattr(app_state, attr, new_val)

            imgui.table_next_column()  # spacer — keeps fw value right-aligned

            imgui.table_next_column()
            fw_val = getattr(app_state, fw_attr)
            if fw_val is None:
                imgui.push_style_color(imgui.Col_.text, _UNKNOWN_COL)
                imgui.text("  —")
            else:
                edit_val = getattr(app_state, attr)
                close = abs(fw_val - edit_val) < 1e-4
                imgui.push_style_color(imgui.Col_.text, _MATCH_COL if close else _MISMATCH_COL)
                imgui.text(f"{fw_val:.4f}")
            imgui.pop_style_color()

        imgui.end_table()

    imgui.spacing()
    imgui.separator()
    imgui.spacing()

    # ---- Action buttons -----------------------------------------------------
    avail_w = imgui.get_content_region_avail().x
    btn_w = (avail_w - imgui.get_style().item_spacing.x * 4) / 5

    if not tcp.connected:
        imgui.begin_disabled()

    if imgui.button("Apply Gains", imgui.ImVec2(btn_w, 0)):
        app_state.cmd_set_gains()

    imgui.same_line()
    if imgui.button("Set Setpoint", imgui.ImVec2(btn_w, 0)):
        app_state.cmd_set_setpoint()

    imgui.same_line()
    if imgui.button("Apply Coast", imgui.ImVec2(btn_w, 0)):
        app_state.cmd_set_coast()

    imgui.same_line()
    if imgui.button("Reset PID", imgui.ImVec2(btn_w, 0)):
        app_state.cmd_reset_pid()

    imgui.same_line()
    if imgui.button("Read from BOB", imgui.ImVec2(btn_w, 0)):
        app_state.cmd_get_params()

    if not tcp.connected:
        imgui.end_disabled()

    # Load button — only shown when firmware values differ from edit fields
    has_fw = app_state.firmware_kp is not None
    if has_fw:
        differs = any(
            abs(getattr(app_state, fw) - getattr(app_state, ed)) > 1e-4
            for _, ed, fw, *_ in _GAINS
            if getattr(app_state, fw) is not None
        )
        if differs:
            imgui.spacing()
            imgui.push_style_color(imgui.Col_.text, _MISMATCH_COL)
            imgui.text("Fields differ from BOB.")
            imgui.pop_style_color()
            imgui.same_line()
            if imgui.small_button("Load BOB values into fields"):
                app_state.cmd_load_params_to_fields()

    # ---- Error display ------------------------------------------------------
    imgui.spacing()
    if tcp.last_error:
        imgui.push_style_color(imgui.Col_.text, imgui.ImVec4(1.0, 0.35, 0.35, 1.0))
        imgui.text_wrapped(f"Error: {tcp.last_error}")
        imgui.pop_style_color()
    elif not has_fw and tcp.connected:
        imgui.text_disabled("Press \"Read from BOB\" to verify gains.")

    imgui.pop_item_width()
