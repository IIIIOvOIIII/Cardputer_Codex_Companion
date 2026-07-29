from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
KEYBOARD_PROBE_HPP = ROOT / "firmware/main/probe/keyboard_probe.hpp"
KEYBOARD_PROBE_CPP = ROOT / "firmware/main/probe/keyboard_probe.cpp"
PRODUCT_CONTROLLER_CPP = ROOT / "firmware/main/product/product_controller.cpp"


def test_keyboard_complete_reports_are_serialized_on_esp():
    header = KEYBOARD_PROBE_HPP.read_text(encoding="utf-8")
    source = KEYBOARD_PROBE_CPP.read_text(encoding="utf-8")

    assert "#include \"freertos/semphr.h\"" in header
    assert "void initialize_report_sink_mutex();" in header
    assert "StaticSemaphore_t report_sink_mutex_storage_{};" in header
    assert "SemaphoreHandle_t report_sink_mutex_ = nullptr;" in header

    assert "initialize_report_sink_mutex();" in source
    compact_source = "".join(source.split())
    assert (
        "xSemaphoreCreateMutexStatic(&report_sink_mutex_storage_)"
        in compact_source
    )

    send_report_start = source.index("void KeyboardProbe::send_report")
    send_report_end = source.index("void KeyboardProbe::emit_stable_key_event")
    send_report_body = source[send_report_start:send_report_end]
    assert "xSemaphoreTake(report_sink_mutex_, pdMS_TO_TICKS(1000))" in send_report_body
    assert "report_sink_->send_report(report);" in send_report_body
    assert "xSemaphoreGive(report_sink_mutex_);" in send_report_body


def test_complete_reports_are_dispatched_by_the_hid_sender_task_on_esp():
    header = KEYBOARD_PROBE_HPP.read_text(encoding="utf-8")
    source = KEYBOARD_PROBE_CPP.read_text(encoding="utf-8")

    assert "enum class HidSenderEventKind" in header
    assert "struct HidSenderEvent" in header
    assert "sizeof(HidSenderEvent) * kHidQueueDepth" in header

    send_complete_start = source.index(
        "void KeyboardProbe::send_complete_report"
    )
    send_complete_end = source.index(
        "void KeyboardProbe::release_all", send_complete_start
    )
    send_complete_body = source[send_complete_start:send_complete_end]
    assert "HidSenderEventKind::complete_report" in send_complete_body
    assert "enqueue_hid_sender_event" in send_complete_body

    sender_loop_start = source.index("void KeyboardProbe::hid_sender_loop")
    sender_loop_body = source[sender_loop_start:]
    assert "HidSenderEvent event" in sender_loop_body
    assert "HidSenderEventKind::complete_report" in sender_loop_body
    assert "send_report(event.report)" in sender_loop_body


def test_g0_dual_action_uses_macro_task_and_preserves_microphone_fallback():
    source = PRODUCT_CONTROLLER_CPP.read_text(encoding="utf-8")
    compact = "".join(source.split())

    assert "enumclassMacroInvocationKind:uint8_t" in compact
    assert "g0_dual_action" in source
    assert "G0DispatchResultenqueue_g0_short_press()" in compact
    assert "execute_g0_dual_action(settings,sink)" in compact
    assert "g_device_settings_mutex" in source
    assert "DeviceSettingssnapshot_device_settings()" in compact

    enqueue_start = source.index(
        "G0DispatchResult enqueue_g0_short_press()"
    )
    enqueue_end = source.index("\n}", enqueue_start)
    enqueue_body = source[enqueue_start:enqueue_end]
    assert "xQueueSend(g_macro_queue" in enqueue_body
    assert "enqueue_microphone_event(" in enqueue_body
    assert "MicrophoneRuntimeEvent::g0_click" in enqueue_body
    assert "true" in enqueue_body

    ui_start = source.index("void ui_task(")
    ui_body = source[ui_start:]
    assert "enqueue_g0_short_press();" in ui_body

    hil_start = source.index("void poll_hil_serial_control()")
    hil_end = source.index(
        "void advance_hil_hid_burst()", hil_start
    )
    hil_body = source[hil_start:hil_end]
    assert "HilMicrophoneCommand::g0_click" in hil_body
    assert "enqueue_g0_short_press()" in hil_body
    assert "HIL G0 CLICK %s" in hil_body
