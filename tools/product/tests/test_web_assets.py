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


def test_web_login_preserves_device_error_semantics() -> None:
    script = Path("web/src/app.js").read_text()
    device_api = Path("web/src/device_api.js").read_text()
    builder = Path("scripts/build_web_assets.py").read_text()

    assert "requestDevice(fetch,path,options,token())" in script
    assert "loginErrorMessage(error)" in script
    assert "PIN 错误或设备不可达" not in script
    assert 'class DeviceApiError extends Error' in device_api
    assert '"partition_incompatible"' in device_api
    assert "设备不可达，请检查 IP、Wi-Fi 和证书访问" in device_api
    assert "PIN 错误" in device_api
    assert "设备分区不兼容" in device_api
    assert "设备服务暂不可用，请稍后重试" in device_api
    assert builder.index("device_api.js") < builder.index("app.js")


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


def test_web_ui_configures_optional_g0_dual_action() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()
    device_api = Path("web/src/device_api.js").read_text()

    assert 'id="g0-chord-form"' in html
    assert 'id="g0-chord-enabled"' in html
    assert 'id="g0-chord-capture"' in html
    assert 'id="save-g0-chord"' in html
    assert "G0 双动作" in html
    assert "启用 G0 组合键" in html
    assert "按下 G0 时先发送组合键，再打开或关闭 Mic。" in html
    assert "保存 G0 配置" in html
    assert "function g0ChordPayload" in device_api
    assert "let g0ChordDraft=" in script
    assert "/api/v1/settings/g0-chord" in script
    assert "captureG0Chord" in script
    assert "loadG0Chord" in script
    assert "saveG0Chord" in script
    assert 'showResult("success","G0 双动作配置已保存")' in script
    assert 'showResult("error",`G0 配置保存失败：${error.message}`)' in script
    assert '$("g0-chord-form").onsubmit=saveG0Chord' in script
    assert '$("g0-chord-capture").onkeydown=captureG0Chord' in script


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


def test_setup_mode_is_public_bounded_and_hides_normal_configuration() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="setup-screen"' in html
    assert 'id="setup-step"' in html
    assert 'id="agent-install-link"' in html
    assert "/api/v1/setup" in script
    assert "probeSetup" in script
    assert "showSetup" in script
    assert "setup.complete" in script
    assert "showAuth" in script
    assert "showApp" not in script.split("async function probeSetup", 1)[1].split(
        "}", 1
    )[0]


def test_agent_download_uses_platform_and_packaging_base_url() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()
    builder = Path("scripts/build_web_assets.py").read_text()

    assert "__PUBLIC_RELEASE_BASE_URL__" in html
    assert "navigator.userAgentData" in script or "navigator.platform" in script
    assert "release-manifest.json" in script
    assert "windows" in script.lower()
    assert "macos" in script.lower()
    assert "--release-base-url" in builder
    assert "PUBLIC_RELEASE_BASE_URL" in builder


def test_restart_setup_requires_an_explicit_confirmation() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="restart-setup"' in html
    assert "/api/v1/setup/restart" in script
    assert "RUN_SETUP_AGAIN" in script
    assert "confirm(" in script
