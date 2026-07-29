from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
KEYBOARD_PROBE_HPP = ROOT / "firmware/main/probe/keyboard_probe.hpp"
KEYBOARD_PROBE_CPP = ROOT / "firmware/main/probe/keyboard_probe.cpp"


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
