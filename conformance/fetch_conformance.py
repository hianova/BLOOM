#!/usr/bin/env python3
"""
Fetch official Khronos glTF-Sample-Assets & upstream OSS conformance targets
into build/conformance_deps/ (gitignored, never vendored into repo).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import urllib.request
from pathlib import Path
from typing import Callable, List, Tuple

ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "build" / "conformance_deps"
MODELS_DIR = DEPS / "gltf"
BOBC_DIR = DEPS / "bobc"

KHRONOS_MODELS: List[str] = [
    "Box",
    "BoxTextured",
    "BoxInterleaved",
    "CesiumMilkTruck",
    "DamagedHelmet",
    "MetalRoughSpheres",
    "NegativeScaleTest",
    "Avocado",
]


def _is_valid_glb(p: Path) -> bool:
    if not p.is_file() or p.stat().st_size < 64:
        return False
    return p.read_bytes()[:4] == b"glTF"


def _is_valid_clay(p: Path) -> bool:
    if not p.is_file() or p.stat().st_size < 50000:
        return False
    txt = p.read_text(encoding="utf-8", errors="ignore")
    return "CLAY_IMPLEMENTATION" in txt and "Clay_RenderCommandArray" in txt


def _is_valid_peanut(p: Path) -> bool:
    if not p.is_file() or p.stat().st_size < 20000:
        return False
    txt = p.read_text(encoding="utf-8", errors="ignore")
    return "gb_init" in txt and "gb_run_frame" in txt


def _is_valid_gb(p: Path) -> bool:
    return p.is_file() and p.stat().st_size >= 32768


def fetch_file(url: str, dest: Path, validator: Callable[[Path], bool]) -> None:
    if validator(dest):
        print(f"  [cache] {dest.name} ({dest.stat().st_size} B)")
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "BLOOM-Conformance/1.0"})
    data = b""
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                data = resp.read()
            break
        except Exception:
            if attempt == 2:
                raise
    tmp = dest.with_suffix(dest.suffix + ".tmp")
    tmp.write_bytes(data)
    if not validator(tmp):
        tmp.unlink(missing_ok=True)
        raise RuntimeError(f"Validation failed for downloaded {url}")
    tmp.replace(dest)
    print(f"  [fetch] {dest.name} ({dest.stat().st_size} B)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--import-bobc", action="store_true", help="Also run tools/bloom_import.py on all downloaded GLBs")
    args = ap.parse_args()

    targets: List[Tuple[str, Path, Callable[[Path], bool]]] = []
    for name in KHRONOS_MODELS:
        url = f"https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/{name}/glTF-Binary/{name}.glb"
        targets.append((url, MODELS_DIR / f"{name}.glb", _is_valid_glb))
    targets.extend([
        ("https://raw.githubusercontent.com/nicbarker/clay/main/clay.h", DEPS / "clay.h", _is_valid_clay),
        ("https://raw.githubusercontent.com/deltabeard/Peanut-GB/master/peanut_gb.h", DEPS / "peanut_gb.h", _is_valid_peanut),
        ("https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb", DEPS / "dmg-acid2.gb", _is_valid_gb),
    ])

    print("[fetch_conformance] Checking upstream conformance assets...")
    for url, dest, val in targets:
        fetch_file(url, dest, val)

    if args.import_bobc:
        BOBC_DIR.mkdir(parents=True, exist_ok=True)
        importer = ROOT / "tools" / "bloom_import.py"
        for name in KHRONOS_MODELS:
            glb = MODELS_DIR / f"{name}.glb"
            out_bobc = BOBC_DIR / f"{name}.bobc"
            out_blmw = BOBC_DIR / f"{name}_pbr.blmw"
            legacy_json = BOBC_DIR / f"{name}_pbr.json"
            if legacy_json.exists():
                legacy_json.unlink()
            subprocess.run(
                [sys.executable, str(importer), "import", "--glb", str(glb), "--format", "bobc",
                 "--out", str(out_bobc), "--pbr-manifest", str(out_blmw), "--rh-to-lh"],
                check=True,
            )


if __name__ == "__main__":
    main()
