import json
from jsonschema import Draft202012Validator
from pathlib import Path


SCHEMA = json.loads(
    Path("protocol/phase0/hardware-manifest.schema.json").read_text()
)


def validate_manifest(manifest: dict) -> list[str]:
    errors = [
        error.message
        for error in sorted(
            Draft202012Validator(SCHEMA).iter_errors(manifest),
            key=lambda item: list(item.path),
        )
    ]
    if manifest.get("flash_bytes") != 8388608:
        errors.append("flash_bytes must equal 8388608")
    if manifest.get("psram_bytes") != 0:
        errors.append("psram_bytes must equal 0")
    return errors
