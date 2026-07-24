from __future__ import annotations

import importlib.util
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


partitions = load_module(
    "verify_partition_layout", ROOT / "tools/product/verify_partition_layout.py"
)


PRODUCT_LAYOUT = """\
# Name, Type, SubType, Offset, Size, Flags
nvs,data,nvs,0x9000,0x6000,
otadata,data,ota,0xf000,0x2000,
phy_init,data,phy,0x11000,0x1000,
wifi_cfg,data,nvs,0x12000,0x6000,
ota_0,app,ota_0,0x20000,0x300000,
ota_1,app,ota_1,0x320000,0x300000,
storage,data,spiffs,0x620000,0x1e0000,
"""


def test_equivalent_layout_accepts_human_readable_sizes():
    actual = PRODUCT_LAYOUT.replace("0x6000", "24K", 1).replace(
        "0x300000", "3M", 1
    )

    assert partitions.compare_layouts(
        partitions.parse_layout(PRODUCT_LAYOUT),
        partitions.parse_layout(actual),
    ) == []


def test_default_partition_layout_is_rejected():
    default_layout = """\
nvs,data,nvs,0x9000,24K,
phy_init,data,phy,0xf000,4K,
factory,app,factory,0x10000,1M,
"""

    errors = partitions.compare_layouts(
        partitions.parse_layout(PRODUCT_LAYOUT),
        partitions.parse_layout(default_layout),
    )

    assert any("missing partition: otadata" in error for error in errors)
    assert any("unexpected partition: factory" in error for error in errors)
    assert any("phy_init offset" in error for error in errors)
