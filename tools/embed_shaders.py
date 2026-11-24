#!/usr/bin/env python3
"""Compile-time Metal shader embedder for zero-path single-library BLOOM distribution."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Tuple

SHADERS: List[Tuple[str, str]] = [
    ("shaders/bloom_core.metal", "bloom_embedded_core_shader"),
    ("shaders/bloom_grounding.metal", "bloom_embedded_grounding_shader"),
    ("shaders/bloom_pbr.metal", "bloom_embedded_pbr_shader"),
    ("shaders/bloom_sprite.metal", "bloom_embedded_sprite_shader"),
    ("shaders/bloom_vision.metal", "bloom_embedded_vision_shader"),
]


def format_c_bytes(fn_name: str, data: bytes) -> str:
    arr_name = fn_name + "_bytes"
    all_bytes = list(data) + [0]
    lines: List[str] = [f"static const unsigned char {arr_name}[] = {{"]
    for i in range(0, len(all_bytes), 20):
        chunk = ", ".join(f"0x{b:02x}" for b in all_bytes[i : i + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append(f"static inline const char *{fn_name}(void) {{ return (const char *){arr_name}; }}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="Embed BLOOM .metal shaders into C header byte arrays")
    ap.add_argument("--root", type=Path, default=Path("."))
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    blocks: List[str] = [
        "#ifndef BLOOM_EMBEDDED_SHADERS_H",
        "#define BLOOM_EMBEDDED_SHADERS_H",
        "",
    ]
    for rel_path, fn_name in SHADERS:
        p = args.root / rel_path
        txt = p.read_text(encoding="utf-8")
        line_cnt = len(txt.splitlines())
        if line_cnt > 400:
            raise SystemExit(f"Shader {rel_path} exceeds 400 line budget ({line_cnt} lines)")
        blocks.append(format_c_bytes(fn_name, txt.encode("utf-8")))
        blocks.append("")

    blocks.append("#endif /* BLOOM_EMBEDDED_SHADERS_H */")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(blocks) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
