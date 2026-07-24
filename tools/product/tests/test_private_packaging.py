from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[3]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


wifi = load_module(
    "generate_wifi_nvs", ROOT / "tools/product/generate_wifi_nvs.py"
)
merge = load_module(
    "merge_product_image", ROOT / "tools/product/merge_product_image.py"
)


def test_wifi_csv_has_expected_namespace_and_private_mode(tmp_path: Path):
    csv_path = tmp_path / "wifi.csv"
    wifi.write_wifi_csv(csv_path, "example-ssid", "example-password")
    assert csv_path.read_text().splitlines() == [
        "key,type,encoding,value",
        "wifi,namespace,,",
        "ssid,data,string,example-ssid",
        "password,data,string,example-password",
    ]
    assert os.stat(csv_path).st_mode & 0o777 == 0o600


def test_product_segments_include_private_partition_at_fixed_offset():
    paths = merge.ProductPaths(
        bootloader=Path("bootloader.bin"),
        partition_table=Path("partition-table.bin"),
        ota_data=Path("ota.bin"),
        application=Path("application.bin"),
        wifi_nvs=Path("wifi.bin"),
    )
    assert merge.segments(paths) == [
        ("0x0", Path("bootloader.bin")),
        ("0x8000", Path("partition-table.bin")),
        ("0xf000", Path("ota.bin")),
        ("0x12000", Path("wifi.bin")),
        ("0x20000", Path("application.bin")),
    ]
