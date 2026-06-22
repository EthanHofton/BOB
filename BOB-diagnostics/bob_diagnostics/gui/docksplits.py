from imgui_bundle import hello_imgui, imgui

from bob_diagnostics.state.app_state import AppState
from bob_diagnostics.gui.imu_panel import gui_imu
from bob_diagnostics.gui.motor_control_panel import gui_motor_control
from bob_diagnostics.gui.pid_output_panel import gui_pid_output
from bob_diagnostics.gui.pid_tuning_panel import gui_pid_tuning
from bob_diagnostics.gui.connection_panel import gui_connection


def define_docksplits() -> list[hello_imgui.DockingSplit]:
    """
    Layout (approximate):

        ┌─────────────────────────┬─────────────────┐
        │                         │                 │
        │      IMU Sensors        │   PID Output    │
        │       (main, 60%)       │   (right, 40%)  │
        │                         │                 │
        ├─────────────────┬───────┴─────────────────┤
        │  Connection     │      PID Tuning          │
        │  (btm-left 35%) │   (btm-right 65%)        │
        └─────────────────┴──────────────────────────┘
    """

    # 1. Carve a bottom strip (30%) for the control panels.
    split_bottom = hello_imgui.DockingSplit()
    split_bottom.initial_dock = "MainDockSpace"
    split_bottom.new_dock     = "BottomSpace"
    split_bottom.direction    = imgui.Dir.down
    split_bottom.ratio        = 0.30

    # 2. Split the remaining top area: give 40% to the right (PID Output).
    split_right = hello_imgui.DockingSplit()
    split_right.initial_dock = "MainDockSpace"
    split_right.new_dock     = "RightSpace"
    split_right.direction    = imgui.Dir.right
    split_right.ratio        = 0.40

    # 3. Split the bottom strip: give 65% to the right (PID Tuning).
    split_bottom_right = hello_imgui.DockingSplit()
    split_bottom_right.initial_dock = "BottomSpace"
    split_bottom_right.new_dock     = "BottomRightSpace"
    split_bottom_right.direction    = imgui.Dir.right
    split_bottom_right.ratio        = 0.65

    return [split_bottom, split_right, split_bottom_right]


def define_dockable_windows(app_state: AppState) -> list[hello_imgui.DockableWindow]:
    imu_window = hello_imgui.DockableWindow()
    imu_window.label          = "IMU Sensors"
    imu_window.dock_space_name = "MainDockSpace"
    imu_window.gui_function   = lambda: gui_imu(app_state)

    pid_out_window = hello_imgui.DockableWindow()
    pid_out_window.label          = "PID Output"
    pid_out_window.dock_space_name = "RightSpace"
    pid_out_window.gui_function   = lambda: gui_pid_output(app_state)

    conn_window = hello_imgui.DockableWindow()
    conn_window.label          = "Connection"
    conn_window.dock_space_name = "BottomSpace"
    conn_window.gui_function   = lambda: gui_connection(app_state)

    tuning_window = hello_imgui.DockableWindow()
    tuning_window.label          = "PID Tuning"
    tuning_window.dock_space_name = "BottomRightSpace"
    tuning_window.gui_function   = lambda: gui_pid_tuning(app_state)

    motor_ctrl_window = hello_imgui.DockableWindow()
    motor_ctrl_window.label          = "Motor Control"
    motor_ctrl_window.dock_space_name = "BottomRightSpace"
    motor_ctrl_window.gui_function   = lambda: gui_motor_control(app_state)

    return [imu_window, pid_out_window, conn_window, tuning_window, motor_ctrl_window]
