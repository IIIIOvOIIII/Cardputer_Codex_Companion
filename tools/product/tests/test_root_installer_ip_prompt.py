import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def test_packaged_root_installer_prompts_for_device_ip(tmp_path):
    package = tmp_path / "package"
    installer = package / "installer"
    installer.mkdir(parents=True)
    shutil.copy2(ROOT / "install.sh", package / "install.sh")
    shutil.copy2(
        ROOT / "scripts/mac_installer.py",
        installer / "mac_installer.py",
    )
    shutil.copy2(
        ROOT / "scripts/install_companion_launch_agent.py",
        installer / "install_companion_launch_agent.py",
    )
    (package / "install.sh").chmod(0o755)
    (package / "CardputerCompanion.app").mkdir()

    environment = dict(os.environ)
    environment["CARDPUTER_MAC_INSTALL_TEST_ROOT"] = str(
        tmp_path / "test-root"
    )
    result = subprocess.run(
        [str(package / "install.sh"), "install"],
        cwd=package,
        env=environment,
        input="192.168.1.195\n87654321\n",
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    assert "Cardputer IP: " in result.stdout
    assert "Cardputer HTTPS URL: " not in result.stdout
