#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "firmware/main/product/web_assets.hpp"


def render() -> str:
    html = (ROOT / "web/src/index.html").read_text()
    css = (ROOT / "web/src/style.css").read_text()
    js = (ROOT / "web/src/app.js").read_text()
    document = html.replace("<!--STYLE-->", f"<style>{css}</style>")
    document = document.replace("<!--SCRIPT-->", f"<script>{js}</script>")
    chunks = [document[i : i + 512] for i in range(0, len(document), 512)]
    body = "\n".join(f"    {json.dumps(chunk)}" for chunk in chunks)
    return (
        "#pragma once\n\n"
        "#include <cstddef>\n\n"
        "inline constexpr char kProductWebHtml[] =\n"
        f"{body};\n"
        "inline constexpr std::size_t kProductWebHtmlSize = "
        "sizeof(kProductWebHtml) - 1;\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = render()
    if args.check:
        return 0 if OUTPUT.exists() and OUTPUT.read_text() == expected else 1
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(expected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
