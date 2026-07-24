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


def test_generated_asset_header_is_current() -> None:
    import subprocess

    subprocess.run(
        ["python3", "scripts/build_web_assets.py", "--check"],
        check=True,
    )
