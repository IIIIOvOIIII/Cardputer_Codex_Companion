from pathlib import Path


def test_web_ui_exposes_required_profile_controls() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()
    assert "Cardputer Codex Companion" in html
    assert 'id="keyboard"' in html
    assert 'id="action-kind"' in html
    for kind in ("passthrough", "hid_chord", "text_utf8", "input_sequence"):
        assert kind in html
    assert "/api/v1/status" in script
    assert "/api/v1/profile" in script
    assert "/api/v1/wifi" in script


def test_web_ui_starts_with_pin_authentication() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="auth-screen"' in html
    assert 'id="app-shell"' in html
    assert 'id="pin-form"' in html
    assert 'id="login-pin"' in html
    assert "先输入设备 PIN" in html
    assert "class=\"hidden\"" in html
    assert "authenticate" in script
    assert "showApp" in script


def test_web_ui_has_settings_and_pin_api() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="tab-keyboard"' in html
    assert 'id="tab-settings"' in html
    assert 'id="settings-view"' in html
    assert 'id="change-pin"' in html
    assert "/api/v1/pin" in script
    assert "修改 PIN" in html
    assert "Wi-Fi 配置" in html


def test_web_ui_uses_chinese_action_labels_and_key_modal() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="key-modal"' in html
    assert 'id="chord-capture"' in html
    assert "组合键" in html
    assert "中文字符串" in html
    assert "Codex 动作" in html
    assert "动作摘要" in script
    assert "describeAction" in script
    assert "openKeyModal" in script
    assert "captureChord" in script
    assert "Alt+V" in html


def test_generated_asset_header_is_current() -> None:
    import subprocess

    subprocess.run(
        ["python3", "scripts/build_web_assets.py", "--check"],
        check=True,
    )
