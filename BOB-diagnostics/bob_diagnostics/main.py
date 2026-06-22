from imgui_bundle import hello_imgui, implot

from bob_diagnostics.gui.docksplits import define_dockable_windows, define_docksplits
from bob_diagnostics.gui.status_bar import show_status_bar
from bob_diagnostics.state.app_state import AppState
from bob_diagnostics.state.tcp_client import TcpClient
from bob_diagnostics.state.ws_client import WsClient


def gui() -> None:
    app_state = AppState(ws=WsClient(), tcp=TcpClient())

    implot.create_context()

    runner_params = hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "BOB Diagnostics"
    runner_params.app_window_params.window_geometry.size = (1400, 860)
    runner_params.app_window_params.borderless = False

    runner_params.ini_filename = "BOB_Diagnostics.ini"

    runner_params.imgui_window_params.show_status_bar = True
    runner_params.imgui_window_params.show_status_fps = False  # shown in our bar
    runner_params.callbacks.show_status = lambda: show_status_bar(app_state)

    runner_params.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.provide_full_screen_dock_space
    )
    runner_params.imgui_window_params.enable_viewports = True

    docking_params = hello_imgui.DockingParams()
    docking_params.docking_splits   = define_docksplits()
    docking_params.dockable_windows = define_dockable_windows(app_state)
    runner_params.docking_params = docking_params

    runner_params.callbacks.pre_new_frame = lambda: app_state.poll()
    runner_params.fps_idling.enable_idling = False

    try:
        hello_imgui.run(runner_params)
    except Exception as e:
        print(f"GUI quit: {repr(e)}")
    finally:
        app_state.ws.disconnect()
        app_state.tcp.disconnect()
        implot.destroy_context()


def main() -> None:
    gui()


if __name__ == "__main__":
    main()
