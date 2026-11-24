#!/usr/bin/env python3
"""
BLOOM vNext SSOT Format Information Density & MDL Evaluator (via sibling ../INFO CLI)
======================================================================================
Executes ../build/format_ssot_bench to emit all candidate 3D/2D SSOT formats with
128-bit bit-exact roundtrip verification, then measures intrinsic information density
(bpb, eta%), Hutter INF1 compressed size, SHA-256 zero-heap roundtrip fidelity,
zlib-9 / lzma-9 baselines, multi-scene boilerplate (eval-mdl-group), hot-reload
edit locality (ncd), and isolated 2D HUD SVG vs Binary density using /Users/kuangtalin/Documents/INFO.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any, Dict, List

PROBES_DIR = Path(__file__).resolve().parent
BUILD_DIR = PROBES_DIR.parent / "build"
DEFAULT_BENCH_BIN = BUILD_DIR / "format_ssot_bench"
DEFAULT_CORPUS_DIR = BUILD_DIR / "fmt_corpus"
DEFAULT_META_JSON = BUILD_DIR / "fmt_codec_meta.json"
DEFAULT_OUT_JSON = BUILD_DIR / "format_info_results.json"

INFO_DIR = Path("/Users/kuangtalin/Documents/INFO")
INFO_BIN = INFO_DIR / "build" / "info"
INFO_DECOMP_BIN = INFO_DIR / "build" / "info_decomp"

Q12_BITS = 4096.0
Q12_BYTES = 4096.0 * 8.0


def run_cmd(cmd: List[str], check_msg: str) -> subprocess.CompletedProcess:
    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if res.returncode != 0:
        sys.stderr.write(f"ERROR ({check_msg}): cmd={' '.join(cmd)}\nstdout:\n{res.stdout}\nstderr:\n{res.stderr}\n")
        sys.exit(1)
    return res


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def measure_single_file(file_path: Path, tmp_dir: Path, stem: str) -> Dict[str, Any]:
    raw_data = file_path.read_bytes()
    orig_bytes = len(raw_data)
    raw_sha = sha256_bytes(raw_data)

    eval_json = tmp_dir / f"{stem}_eval.json"
    inf_path = tmp_dir / f"{stem}.inf"
    dec_path = tmp_dir / f"{stem}.dec"

    run_cmd([str(INFO_BIN), "eval-bpb", str(file_path), "--out-json", str(eval_json)], f"eval-bpb {file_path.name}")
    ev = json.loads(eval_json.read_text(encoding="utf-8"))

    run_cmd([str(INFO_BIN), "compress", str(file_path), str(inf_path)], f"compress {file_path.name}")
    run_cmd([str(INFO_DECOMP_BIN), str(inf_path), str(dec_path)], f"info_decomp {file_path.name}")

    dec_data = dec_path.read_bytes()
    dec_sha = sha256_bytes(dec_data)
    if raw_sha != dec_sha:
        sys.stderr.write(f"FATAL SHA-256 MISMATCH after info_decomp on {file_path.name}\n")
        sys.exit(1)

    inf1_bytes = inf_path.stat().st_size
    info_bits = ev["total_bits_q12"] / Q12_BITS
    info_bytes = info_bits / 8.0
    bpb = info_bits / max(1, orig_bytes)
    eta_pct = (bpb / 8.0) * 100.0

    zlib9_bytes = len(zlib.compress(raw_data, level=9))
    lzma9_bytes = len(lzma.compress(raw_data, preset=9))

    return {
        "orig_bytes": orig_bytes,
        "info_bits": info_bits,
        "info_bytes": info_bytes,
        "bpb": bpb,
        "info_density_pct": eta_pct,
        "inf1_bytes": inf1_bytes,
        "inf1_bpb": (inf1_bytes * 8.0) / max(1, orig_bytes),
        "zlib9_bytes": zlib9_bytes,
        "lzma9_bytes": lzma9_bytes,
        "alg_hits": ev["alg_hits"],
        "match_hits": ev["match_hits"],
        "max_surprise_bpb": (ev["max_surprise_q12"] / Q12_BITS),
        "sha256_ok": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate BLOOM SSOT format information density via ../INFO")
    parser.add_argument("--bench-bin", type=Path, default=DEFAULT_BENCH_BIN)
    parser.add_argument("--corpus-dir", type=Path, default=DEFAULT_CORPUS_DIR)
    parser.add_argument("--meta-json", type=Path, default=DEFAULT_META_JSON)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    args = parser.parse_args()

    if not INFO_BIN.is_file() or not INFO_DECOMP_BIN.is_file():
        sys.stderr.write(f"ERROR: INFO binaries not found at {INFO_BIN} / {INFO_DECOMP_BIN}\n")
        sys.exit(1)

    # Gate 0: Verify INFO selftest & emit fresh C++17 SSOT corpus
    run_cmd([str(INFO_BIN), "selftest"], "INFO selftest")
    res = run_cmd(
        [str(args.bench_bin), "--out-dir", str(args.corpus_dir), "--meta-json", str(args.meta_json)],
        "format_ssot_bench",
    )
    print(res.stdout.strip())

    meta = json.loads(args.meta_json.read_text(encoding="utf-8"))
    records: List[Dict[str, Any]] = meta["records"]
    hud_records: List[Dict[str, Any]] = meta["hud_records"]

    with tempfile.TemporaryDirectory(prefix="bloom_info_eval_") as tmp_s:
        tmp_dir = Path(tmp_s)

        print("\n=== [1/4] Per-Format Information Density (eval-bpb + INF1 + info_decomp SHA-256 + decode us) ===")
        for idx, rec in enumerate(records):
            fpath = args.corpus_dir / rec["file"]
            m = measure_single_file(fpath, tmp_dir, f"rec_{idx}")
            rec.update(m)
            ent = max(1, rec["entities"])
            rec["raw_bytes_per_entity"] = rec["orig_bytes"] / ent
            rec["mdl_bytes_per_entity"] = rec["inf1_bytes"] / ent

        for sc in ("s1_micro", "s2_work", "s3_stage"):
            for ly in ("full", "scene"):
                print(f"\n  -- {sc} [{ly}] --")
                rows = [r for r in records if r["scene"] == sc and r["layer"] == ly]
                for r in rows:
                    print(
                        f"  {r['format']:<16s} | raw={r['orig_bytes']:>7d} B | bpb={r['bpb']:5.3f} (eta={r['info_density_pct']:5.1f}%) "
                        f"| INF1={r['inf1_bytes']:>6d} B | zlib9={r['zlib9_bytes']:>6d} B | lzma9={r['lzma9_bytes']:>6d} B "
                        f"| enc={r['encode_med_us']:6.1f} us | dec={r['decode_med_us']:6.1f} us | B/ent={r['raw_bytes_per_entity']:6.1f}"
                    )

        print("\n=== [2/4] Multi-Scene Boilerplate Evaluation (info eval-mdl-group on G0..G3) ===")
        group_formats = [
            ("baseline_glb64", "full", "glb"),
            ("baseline_glb64", "scene", "scene.glb"),
            ("3a_xml_bobj", "full", "full.b3a"),
            ("3a_xml_bobj", "scene", "scene.b3a"),
            ("3b_svg_scene", "full", "full.b3b"),
            ("3b_svg_scene", "scene", "scene.svg"),
            ("3c_bobj_row", "full", "full.bobr"),
            ("3c_bobj_row", "scene", "scene.bobr"),
            ("3c_bobj_col", "full", "full.bobc"),
            ("3c_bobj_col", "scene", "scene.bobc"),
        ]
        group_results: List[Dict[str, Any]] = []
        for gi, (fmt, ly, ext) in enumerate(group_formats):
            files = [str(args.corpus_dir / f"g{k}__{fmt}.{ext}") for k in range(4)]
            gjson = tmp_dir / f"grp_{gi}.json"
            run_cmd([str(INFO_BIN), "eval-mdl-group", "--out-json", str(gjson), *files], f"eval-mdl-group {fmt}.{ext}")
            gj = json.loads(gjson.read_text(encoding="utf-8"))
            sum_indep_b = gj["sum_indep_bits_q12"] / Q12_BYTES
            seq_joint_b = gj["seq_joint_bits_q12"] / Q12_BYTES
            savings_b = gj["joint_savings_bits_q12"] / Q12_BYTES
            savings_pct = (savings_b / max(1e-9, sum_indep_b)) * 100.0
            raw_sum = sum(Path(f).stat().st_size for f in files)
            entry = {
                "format": fmt,
                "layer": ly,
                "raw_group_bytes": raw_sum,
                "sum_indep_bytes": sum_indep_b,
                "seq_joint_bytes": seq_joint_b,
                "joint_savings_bytes": savings_b,
                "savings_pct": savings_pct,
            }
            group_results.append(entry)
            print(
                f"  {fmt:<16s} [{ly:<5s}] | raw_4x={raw_sum:>7d} B | indep={sum_indep_b:8.1f} B "
                f"| joint={seq_joint_b:8.1f} B | boilerplate_savings={savings_b:8.1f} B ({savings_pct:5.2f}%)"
            )

        print("\n=== [3/4] Hot-Reload Edit Locality (info ncd S2_v1 -> S2_v2) ===")
        ncd_results: List[Dict[str, Any]] = []
        for ni, (fmt, ly, ext) in enumerate(group_formats):
            f1 = args.corpus_dir / f"s2_work__{fmt}.{ext}"
            f2 = args.corpus_dir / f"s2_work_v2__{fmt}.{ext}"
            njson = tmp_dir / f"ncd_{ni}.json"
            run_cmd([str(INFO_BIN), "ncd", str(f1), str(f2), "--out-json", str(njson)], f"ncd {fmt}.{ext}")
            nj = json.loads(njson.read_text(encoding="utf-8"))
            la = nj["la_bits_q12"] / Q12_BYTES
            lb = nj["lb_bits_q12"] / Q12_BYTES
            lba = nj["lb_given_a_bits_q12"] / Q12_BYTES
            lab = nj["la_given_b_bits_q12"] / Q12_BYTES
            ncd_val = max(0.0, (min(la + lba, lb + lab) - min(la, lb)) / max(1e-9, max(la, lb)))
            entry = {
                "format": fmt,
                "layer": ly,
                "v1_bytes": nj["len_a"],
                "v2_bytes": nj["len_b"],
                "raw_delta_bytes": nj["len_b"] - nj["len_a"],
                "conditional_v2_given_v1_bytes": lba,
                "ncd": ncd_val,
            }
            ncd_results.append(entry)
            print(
                f"  {fmt:<16s} [{ly:<5s}] | v1={nj['len_a']:>6d} B -> v2={nj['len_b']:>6d} B "
                f"| K(v2|v1)={lba:6.1f} B | NCD={ncd_val:.4f}"
            )

        print("\n=== [4/4] Isolated 2D HUD Format Density (S3 128 HUD nodes: W3C SVG vs Binary Row vs Columnar) ===")
        for hi, hrec in enumerate(hud_records):
            fpath = args.corpus_dir / hrec["file"]
            hm = measure_single_file(fpath, tmp_dir, f"hud_{hi}")
            hrec.update(hm)
            hrec["raw_bytes_per_hud"] = hrec["orig_bytes"] / max(1, hrec["hud_count"])
            hrec["mdl_bytes_per_hud"] = hrec["inf1_bytes"] / max(1, hrec["hud_count"])
            print(
                f"  {hrec['format']:<12s} | raw={hrec['orig_bytes']:>6d} B ({hrec['raw_bytes_per_hud']:5.1f} B/node) "
                f"| bpb={hrec['bpb']:5.3f} (eta={hrec['info_density_pct']:5.1f}%) "
                f"| INF1={hrec['inf1_bytes']:>5d} B | lzma9={hrec['lzma9_bytes']:>5d} B "
                f"| enc={hrec['encode_med_us']:5.1f} us | dec={hrec['decode_med_us']:5.1f} us"
            )

    out_payload = {
        "info_tool": str(INFO_BIN),
        "info_decomp_tool": str(INFO_DECOMP_BIN),
        "all_semantic_roundtrips_ok": all(r["roundtrip_ok"] for r in records + hud_records),
        "all_sha256_decomp_ok": all(r["sha256_ok"] for r in records + hud_records),
        "records": records,
        "group_mdl": group_results,
        "hot_reload_ncd": ncd_results,
        "hud_isolated": hud_records,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(out_payload, indent=2), encoding="utf-8")
    print(f"\nWrote INFO format evaluation JSON results to {args.out_json}")


if __name__ == "__main__":
    main()
