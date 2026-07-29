from pathlib import Path
import struct

import pytest

from tools.product.package_launcher_image import pad_launcher_image
from tools.product.verify_launcher_firmware import (
    LAUNCHER_IMAGE_SIZE,
    LAUNCHER_STORAGE_LABEL,
    STORAGE_BOUNDARY,
    STORAGE_PAYLOAD_BYTES,
    validate_launcher_image,
)
from tools.product.verify_launcher_app_size import (
    M5LAUNCHER_CARDPU_PARTITION_SIZE,
    validate_launcher_app_size,
)


def partition_entry(
    partition_type: int,
    subtype: int,
    offset: int,
    size: int,
    label: str,
) -> bytes:
    encoded = label.encode("ascii")
    return struct.pack(
        "<HBBII16sI",
        0x50AA,
        partition_type,
        subtype,
        offset,
        size,
        encoded + b"\0" * (16 - len(encoded)),
        0,
    )


def compatible_launcher_image() -> bytearray:
    image = bytearray(b"\xff" * LAUNCHER_IMAGE_SIZE)
    table = b"".join(
        (
            partition_entry(0, 0x10, 0x20000, 0x300000, "ota_0"),
            partition_entry(
                1,
                0x82,
                STORAGE_BOUNDARY,
                0x1E0000,
                LAUNCHER_STORAGE_LABEL,
            ),
        )
    )
    image[0x8000 : 0x8000 + len(table)] = table
    image[0x20000 : 0x20004] = b"\xe9\x01\x00\x00"
    return image


def test_launcher_image_carries_erased_payload_for_dynamic_storage_creation(
    tmp_path: Path,
) -> None:
    source = tmp_path / "merged.bin"
    source.write_bytes(b"\xe9\x01" + b"\xff" * 30)
    output = tmp_path / "launcher.bin"

    pad_launcher_image(source, output)

    assert output.stat().st_size == LAUNCHER_IMAGE_SIZE
    assert output.read_bytes()[:32] == source.read_bytes()
    assert output.read_bytes()[-STORAGE_PAYLOAD_BYTES:] == (
        b"\xff" * STORAGE_PAYLOAD_BYTES
    )


def test_launcher_image_rejects_content_that_reaches_storage(
    tmp_path: Path,
) -> None:
    source = tmp_path / "oversized.bin"
    source.write_bytes(b"\xff" * (0x620000 + 1))

    with pytest.raises(ValueError, match="storage boundary"):
        pad_launcher_image(source, tmp_path / "launcher.bin")


def test_launcher_contract_accepts_declared_assets_storage_with_payload() -> None:
    validate_launcher_image(bytes(compatible_launcher_image()))


def test_launcher_contract_rejects_non_erased_wifi_configuration() -> None:
    image = compatible_launcher_image()
    image[0x12000] = 0

    with pytest.raises(ValueError, match="Wi-Fi configuration"):
        validate_launcher_image(bytes(image))


def test_launcher_contract_rejects_missing_storage_label() -> None:
    image = compatible_launcher_image()
    image[0x8000 + 32 + 12 : 0x8000 + 32 + 19] = b"storage"

    with pytest.raises(ValueError, match="storage partition"):
        validate_launcher_image(bytes(image))


def test_launcher_contract_rejects_small_storage_declaration() -> None:
    image = compatible_launcher_image()
    image[0x8000 + 32 + 8 : 0x8000 + 32 + 12] = struct.pack(
        "<I", 0x1D0000
    )

    with pytest.raises(ValueError, match="storage partition"):
        validate_launcher_image(bytes(image))


def test_launcher_app_fits_existing_m5launcher_cardpu_partition(
    tmp_path: Path,
) -> None:
    fitting = tmp_path / "fitting.bin"
    fitting.write_bytes(b"\xff" * M5LAUNCHER_CARDPU_PARTITION_SIZE)
    validate_launcher_app_size(fitting)

    oversized = tmp_path / "oversized.bin"
    oversized.write_bytes(
        b"\xff" * (M5LAUNCHER_CARDPU_PARTITION_SIZE + 1)
    )
    with pytest.raises(ValueError, match="M5Launcher cardpu partition"):
        validate_launcher_app_size(oversized)


def test_launcher_build_uses_launcher_assets_partition_contract() -> None:
    root = Path(__file__).resolve().parents[3]
    partition_csv = (root / "firmware/partitions_launcher.csv").read_text()
    package_script = (root / "scripts/package_product_firmware.sh").read_text()
    component_cmake = (root / "firmware/main/CMakeLists.txt").read_text()
    label_header = (
        root / "firmware/main/product/storage_partition_label.hpp"
    ).read_text()
    storage_sources = [
        (root / "firmware/main/product/storage_compatibility.cpp").read_text(),
        (root / "firmware/main/product/pet_store.cpp").read_text(),
        (root / "firmware/main/product/profile_catalog.cpp").read_text(),
    ]

    assert "assets" in partition_csv
    assert "0x620000" in partition_csv
    assert "0x1e0000" in partition_csv
    assert "CARDPUTER_LAUNCHER_BUILD=ON" in package_script
    assert "sdkconfig.launcher.defaults" in package_script
    assert "verify_launcher_app_size.py" in package_script
    assert 'target_compile_options(${COMPONENT_LIB} PRIVATE "-Os")' in (
        component_cmake
    )
    assert "CARDPUTER_STORAGE_PARTITION_LABEL" in component_cmake
    assert '"assets"' in component_cmake
    assert '"storage"' in label_header
    assert all(
        "kProductStoragePartitionLabel" in source
        for source in storage_sources
    )
