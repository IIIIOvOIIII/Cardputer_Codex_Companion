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


def test_web_ui_starts_with_masked_pin_authentication() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="auth-screen"' in html
    assert 'id="app-shell"' in html
    assert 'id="pin-form"' in html
    assert 'id="login-pin" type="password"' in html
    assert 'inputmode="numeric"' in html
    assert 'maxlength="8"' in html
    assert 'pattern="[0-9]{8}"' in html
    assert "Codex Companion Login" in html
    assert "请输入设备PIN码进行鉴权" in html
    assert "先输入设备 PIN" not in html
    assert "PIN 显示在 Cardputer 屏幕上" not in html
    assert "authenticate" in script
    assert "showApp" in script


def test_login_spacing_and_result_dialog_structure() -> None:
    html = Path("web/src/index.html").read_text()
    css = Path("web/src/style.css").read_text()

    assert "#pin-form" in css
    assert "gap:16px" in css
    assert "margin-top:18px" in css
    assert 'id="result-modal"' in html
    assert 'role="alertdialog"' in html
    assert 'id="result-title"' in html
    assert 'id="result-message"' in html
    assert 'id="result-close"' in html
    assert ".result-modal" in css
    assert "z-index:20" in css


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
    assert 'id="microphone-status"' in html
    assert "麦克风状态（只读）" in html
    for field in ("mic-state", "mic-rate", "mic-drop", "mic-error"):
        assert f'id="{field}"' in html
    assert "showMicrophoneStatus(state.microphone)" in script
    assert "microphone/start" not in script
    assert "microphone/stop" not in script


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


def test_key_editor_publishes_with_result_dialog_and_rollback() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert ">保存并发布</button>" in html
    assert "let resultTimer=0" in script
    assert "function showResult(kind,message)" in script
    assert '$("result-message").textContent=message' in script
    assert "resultTimer=setTimeout(closeResult,1500)" in script
    assert 'showResult("success","键位配置已发布到设备")' in script
    assert 'showResult("error",`修改失败：${error.message}`)' in script
    assert "profile.bindings[index]=previous" in script
    assert "async function restorePassthrough()" in script
    assert "await publishProfile(false)" in script
    assert '$("delete-mapping").onclick=restorePassthrough' in script


def test_generated_asset_header_is_current() -> None:
    import subprocess

    subprocess.run(
        ["python3", "scripts/build_web_assets.py", "--check"],
        check=True,
    )
