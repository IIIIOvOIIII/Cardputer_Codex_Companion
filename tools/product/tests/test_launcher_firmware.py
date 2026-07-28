from pathlib import Path
import struct

import pytest

from tools.product.package_launcher_image import pad_launcher_image
from tools.product.verify_launcher_firmware import (
    STORAGE_BOUNDARY,
    validate_launcher_image,
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
    image = bytearray(b"\xff" * STORAGE_BOUNDARY)
    table = b"".join(
        (
            partition_entry(0, 0x10, 0x20000, 0x300000, "ota_0"),
            partition_entry(1, 0x82, 0x620000, 0x1E0000, "storage"),
        )
    )
    image[0x8000 : 0x8000 + len(table)] = table
    image[0x20000 : 0x20004] = b"\xe9\x01\x00\x00"
    return image


def test_launcher_image_is_padded_with_erased_flash_to_storage_boundary(
    tmp_path: Path,
) -> None:
    source = tmp_path / "merged.bin"
    source.write_bytes(b"\xe9\x01" + b"\xff" * 30)
    output = tmp_path / "launcher.bin"

    pad_launcher_image(source, output)

    assert output.stat().st_size == 0x620000
    assert output.read_bytes()[:32] == source.read_bytes()
    assert output.read_bytes()[-4096:] == b"\xff" * 4096


def test_launcher_image_rejects_content_that_reaches_storage(
    tmp_path: Path,
) -> None:
    source = tmp_path / "oversized.bin"
    source.write_bytes(b"\xff" * (0x620000 + 1))

    with pytest.raises(ValueError, match="storage boundary"):
        pad_launcher_image(source, tmp_path / "launcher.bin")


def test_launcher_contract_accepts_declared_empty_storage() -> None:
    validate_launcher_image(bytes(compatible_launcher_image()))


def test_launcher_contract_rejects_non_erased_wifi_configuration() -> None:
    image = compatible_launcher_image()
    image[0x12000] = 0

    with pytest.raises(ValueError, match="Wi-Fi configuration"):
        validate_launcher_image(bytes(image))


def test_launcher_contract_rejects_missing_storage_label() -> None:
    image = compatible_launcher_image()
    image[0x8000 + 32 + 12 : 0x8000 + 32 + 19] = b"spiffs\0"

    with pytest.raises(ValueError, match="storage partition"):
        validate_launcher_image(bytes(image))


def test_launcher_contract_rejects_small_storage_declaration() -> None:
    image = compatible_launcher_image()
    image[0x8000 + 32 + 8 : 0x8000 + 32 + 12] = struct.pack(
        "<I", 0x1D0000
    )

    with pytest.raises(ValueError, match="storage partition"):
        validate_launcher_image(bytes(image))
