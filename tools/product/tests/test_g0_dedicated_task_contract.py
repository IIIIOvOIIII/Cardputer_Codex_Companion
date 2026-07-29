from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CONTROLLER = ROOT / "firmware/main/product/product_controller.cpp"


def test_enabled_g0_uses_a_dedicated_static_task_and_queue():
    source = CONTROLLER.read_text(encoding="utf-8")

    assert "constexpr std::size_t kG0QueueDepth = 8;" in source
    assert "constexpr std::size_t kG0TaskStackBytes = 3072;" in source
    assert 'g0_task, "product-g0"' in source
    assert "xQueueSend(g_g0_queue, &invocation, 0)" in source
    assert "MacroInvocationKind::g0_dual_action" not in source


def test_profile_and_g0_workers_share_execution_serialization():
    source = CONTROLLER.read_text(encoding="utf-8")

    assert "g_macro_execution_mutex" in source
    assert (
        source.count("SemaphoreLock execution_lock(g_macro_execution_mutex);")
        == 2
    )
    assert "g0 macro lock fallback" in source


def test_runtime_metrics_expose_g0_stack_headroom():
    source = CONTROLLER.read_text(encoding="utf-8")

    assert r'{\"name\":\"g0-dual\"' in source
    assert "task_stack_free_bytes(g_g0_task_handle)" in source
