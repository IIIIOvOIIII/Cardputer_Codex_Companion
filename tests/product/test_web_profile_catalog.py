from pathlib import Path


ROOT = Path(__file__).parents[2]
HTML = (ROOT / "web/src/index.html").read_text()
JS = (ROOT / "web/src/app.js").read_text()


def test_profile_toolbar_has_catalog_controls():
    for identifier in (
        'id="profile-select"',
        'id="create-profile"',
        'id="clone-profile"',
        'id="rename-profile"',
        'id="delete-profile"',
        'id="activate-profile"',
    ):
        assert identifier in HTML


def test_catalog_and_activation_endpoints_are_used():
    assert 'api("/api/v1/profiles"' in JS
    assert 'api("/api/v1/profile/activate"' in JS


def test_profile_id_is_encoded_with_url_search_params():
    assert "new URLSearchParams" in JS
    assert 'params.set("id"' in JS
    assert '"/api/v1/profile?id="+' not in JS


def test_delete_is_disabled_for_safe_and_active_profile():
    assert 'selectedProfileID==="SAFE"' in JS
    assert "selectedProfileID===activeProfileID" in JS


def test_selected_profile_is_the_only_publish_target():
    assert "profilePath(selectedProfileID)" in JS
    assert 'api("/api/v1/profile",{method:"PUT"' not in JS


def test_profile_results_use_in_page_dialog():
    assert 'showResult("success"' in JS
    assert 'showResult("error"' in JS
    assert "confirm(" in JS


def test_login_and_wifi_secrets_remain_masked():
    assert 'id="login-pin" type="password"' in HTML
    assert 'id="wifi-password" type="password"' in HTML
