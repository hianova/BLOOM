#!/usr/bin/env python3
"""
BLOOM vNext Optional Build-Time Asset Importer (.glb + .svg -> .bobc / .bobr)
=============================================================================
Compiles optional design-time glTF 2.0 (.glb) meshes and W3C SVG HUD fragments
into BLOOM vNext's 3C Dual-Form Binary SSOT (BOBC columnar wire or BOBR row).
Zero runtime XML/JSON overhead escapes into the core GPU engine.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple

CLUSTER_VTX = 64
CLUSTER_TRI = 96
LOCAL_BOX_LO = -1.5
LOCAL_BOX_SPAN = 3.0

MAGIC_ROW = 0x52424F42  # "BOBR"
MAGIC_COL = 0x43424F42  # "BOBC"
FLAG_GEOM = 1


def f32_to_f16(value: float) -> int:
    if value == 0.0:
        value = 0.0
    raw = struct.pack("<f", float(value))
    f = struct.unpack("<I", raw)[0]
    sign = (f >> 16) & 0x8000
    exp = ((f >> 23) & 0xFF) - 127 + 15
    mant = f & 0x7FFFFF
    if (f & 0x7FFFFFFF) == 0:
        return sign
    if exp <= 0:
        if exp < -10:
            return sign
        mant = (mant | 0x800000) >> (1 - exp)
        return sign | ((mant + 0x1000) >> 13)
    if exp >= 31:
        return sign | 0x7C00
    h = sign | (exp << 10) | (mant >> 13)
    if (mant & 0x1FFF) > 0x1000 or ((mant & 0x1FFF) == 0x1000 and (h & 1)):
        h += 1
    return h & 0xFFFF


def f16_to_f32(h: int) -> float:
    return struct.unpack("<e", struct.pack("<H", h & 0xFFFF))[0]


def clamp_sym_i8(v: float) -> int:
    q = int(round(v * 127.0))
    return max(-127, min(127, q))


def pack_pos_unorm16(x: float) -> int:
    t = max(0.0, min(1.0, (x - LOCAL_BOX_LO) / LOCAL_BOX_SPAN))
    return max(0, min(65535, int(round(t * 65535.0))))


def unpack_pos_unorm16(q: int) -> float:
    return LOCAL_BOX_LO + q * (LOCAL_BOX_SPAN / 65535.0)


def oct_encode(nx: float, ny: float, nz: float) -> Tuple[int, int]:
    l1 = abs(nx) + abs(ny) + abs(nz)
    if l1 < 1e-6:
        l1 = 1.0
    ox, oy = nx / l1, ny / l1
    if nz < 0.0:
        sx = 1.0 if ox >= 0.0 else -1.0
        sy = 1.0 if oy >= 0.0 else -1.0
        ox, oy = (1.0 - abs(oy)) * sx, (1.0 - abs(ox)) * sy
    return clamp_sym_i8(ox), clamp_sym_i8(oy)


def put_uvarint(out: bytearray, v: int) -> None:
    while v >= 0x80:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v)


def zz_enc(x: int) -> int:
    return ((x << 1) ^ (x >> 63)) & 0xFFFFFFFFFFFFFFFF


def put_front_strings(out: bytearray, strings: Sequence[str]) -> None:
    prev = b""
    for s in strings:
        raw = s.encode("utf-8")
        lim = min(len(prev), len(raw))
        lcp = 0
        while lcp < lim and prev[lcp] == raw[lcp]:
            lcp += 1
        suf = raw[lcp:]
        put_uvarint(out, lcp)
        put_uvarint(out, len(suf))
        out.extend(suf)
        prev = raw


def put_f32_planes(out: bytearray, vals: Sequence[float]) -> None:
    raws = [struct.unpack("<I", struct.pack("<f", v))[0] for v in vals]
    for lane in (3, 2, 1, 0):
        sh = lane * 8
        for u in raws:
            out.append((u >> sh) & 0xFF)


def put_u16_lanes(out: bytearray, vals: Sequence[int]) -> None:
    for v in vals:
        out.append((v >> 8) & 0xFF)
    for v in vals:
        out.append(v & 0xFF)


def put_u32_lanes(out: bytearray, vals: Sequence[int]) -> None:
    for b in range(4):
        sh = b * 8
        for v in vals:
            out.append((v >> sh) & 0xFF)


def parse_rgba(s: str, default: int = 0xFF40D8F0) -> int:
    s = (s or "").strip()
    if not s:
        return default
    if s.startswith("#"):
        s = s[1:]
    if len(s) == 6:
        r, g, b, a = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), 255
    elif len(s) == 8:
        r, g, b, a = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), int(s[6:8], 16)
    else:
        return default
    return (a << 24) | (b << 16) | (g << 8) | r


# ============================================================================
# glTF 2.0 (.glb) Parser & Meshlet Partitioner (<=64 vtx, <=96 tris)
# ============================================================================
def _read_accessor(gltf: Dict[str, Any], bin_blob: bytes, acc_idx: int) -> List[Tuple[float, ...]]:
    acc = gltf["accessors"][acc_idx]
    bv = gltf["bufferViews"][acc["bufferView"]]
    base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    count = acc["count"]
    ctype = acc["componentType"]
    atype = acc["type"]
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[atype]
    fmt_map = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
    ch = fmt_map[ctype]
    elem_sz = struct.calcsize("<" + ch * ncomp)
    stride = bv.get("byteStride", elem_sz)
    out: List[Tuple[float, ...]] = []
    for i in range(count):
        off = base + i * stride
        out.append(struct.unpack_from("<" + ch * ncomp, bin_blob, off))
    return out


def parse_glb_to_template(glb_path: Path, tpl_idx: int) -> Dict[str, Any]:
    data = glb_path.read_bytes()
    if len(data) < 20 or data[:4] != b"glTF":
        raise ValueError(f"Invalid GLB header in {glb_path}")
    pos = 12
    json_blob = b""
    bin_blob = b""
    while pos + 8 <= len(data):
        clen, ctype = struct.unpack_from("<II", data, pos)
        pos += 8
        chunk = data[pos : pos + clen]
        pos += clen
        if ctype == 0x4E4F534A:  # JSON
            json_blob = chunk
        elif ctype == 0x004E4942:  # BIN
            bin_blob = chunk

    gltf = json.loads(json_blob.decode("utf-8"))
    mesh = gltf["meshes"][0]
    prim = mesh["primitives"][0]
    attrs = prim["attributes"]
    positions = _read_accessor(gltf, bin_blob, attrs["POSITION"])
    normals = _read_accessor(gltf, bin_blob, attrs["NORMAL"]) if "NORMAL" in attrs else None
    uvs = _read_accessor(gltf, bin_blob, attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None

    if "indices" in prim:
        raw_idx = [int(x[0]) for x in _read_accessor(gltf, bin_blob, prim["indices"])]
    else:
        raw_idx = list(range(len(positions)))

    # Compute missing flat normals if glTF lacks NORMAL attribute
    if normals is None:
        acc_n = [[0.0, 0.0, 0.0] for _ in range(len(positions))]
        for t in range(0, len(raw_idx) - 2, 3):
            ia, ib, ic = raw_idx[t], raw_idx[t + 1], raw_idx[t + 2]
            pa, pb, pc = positions[ia], positions[ib], positions[ic]
            e1 = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
            e2 = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
            nx = e1[1] * e2[2] - e1[2] * e2[1]
            ny = e1[2] * e2[0] - e1[0] * e2[2]
            nz = e1[0] * e2[1] - e1[1] * e2[0]
            for idx in (ia, ib, ic):
                acc_n[idx][0] += nx
                acc_n[idx][1] += ny
                acc_n[idx][2] += nz
        normals = []
        for n in acc_n:
            l = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) or 1.0
            normals.append((n[0] / l, n[1] / l, n[2] / l))

    # Fit positions cleanly inside [-1.4, +1.4] domain if needed
    max_abs = max((max(abs(p[0]), abs(p[1]), abs(p[2])) for p in positions), default=1.0)
    norm_scale = (1.35 / max_abs) if max_abs > 1.45 else 1.0

    # Greedy partition triangles into meshlets (<=64 local vertices, <=96 tris)
    meshlets: List[Tuple[List[int], List[int]]] = []
    cur_global_verts: List[int] = []
    cur_map: Dict[int, int] = {}
    cur_local_idx: List[int] = []
    for t in range(0, len(raw_idx) - 2, 3):
        tri = (raw_idx[t], raw_idx[t + 1], raw_idx[t + 2])
        new_needed = sum(1 for gi in tri if gi not in cur_map)
        if (len(cur_local_idx) // 3 >= CLUSTER_TRI) or (len(cur_global_verts) + new_needed > CLUSTER_VTX):
            meshlets.append((cur_global_verts, cur_local_idx))
            cur_global_verts, cur_map, cur_local_idx = [], {}, []
        for gi in tri:
            if gi not in cur_map:
                cur_map[gi] = len(cur_global_verts)
                cur_global_verts.append(gi)
            cur_local_idx.append(cur_map[gi])
    if cur_local_idx:
        meshlets.append((cur_global_verts, cur_local_idx))

    hdrs: List[Dict[str, Any]] = []
    verts: List[Tuple[int, int, int, int, int, int, int, int, int]] = []
    local_idx_stream = bytearray()

    for m_i, (gverts, lidx) in enumerate(meshlets):
        idx_off = len(local_idx_stream)
        local_idx_stream.extend(lidx)
        dec_pos: List[Tuple[float, float, float]] = []
        dec_nrm: List[Tuple[float, float, float]] = []
        for slot in range(CLUSTER_VTX):
            if slot < len(gverts):
                gi = gverts[slot]
                px, py, pz = positions[gi][0] * norm_scale, positions[gi][1] * norm_scale, positions[gi][2] * norm_scale
                nx, ny, nz = normals[gi][0], normals[gi][1], normals[gi][2]
                nl = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                nx, ny, nz = nx / nl, ny / nl, nz / nl
                tu, tv = (uvs[gi][0], uvs[gi][1]) if uvs else (0.0, 0.0)
                qp = (pack_pos_unorm16(px), pack_pos_unorm16(py), pack_pos_unorm16(pz))
                ox, oy = oct_encode(nx, ny, nz)
                uv8 = (max(0, min(255, int(round(tu * 255.0)))), max(0, min(255, int(round(tv * 255.0)))))
                verts.append((qp[0], qp[1], qp[2], ox, oy, uv8[0], uv8[1], m_i & 3, tpl_idx & 7))
                dec_pos.append((unpack_pos_unorm16(qp[0]), unpack_pos_unorm16(qp[1]), unpack_pos_unorm16(qp[2])))
                dec_nrm.append((nx, ny, nz))
            else:
                verts.append((32768, 32768, 32768, 0, 127, 0, 0, 0, 0))

        cx = sum(p[0] for p in dec_pos) / max(1, len(dec_pos))
        cy = sum(p[1] for p in dec_pos) / max(1, len(dec_pos))
        cz = sum(p[2] for p in dec_pos) / max(1, len(dec_pos))
        rad = max((math.sqrt((p[0] - cx) ** 2 + (p[1] - cy) ** 2 + (p[2] - cz) ** 2) for p in dec_pos), default=0.5)
        nsx = sum(n[0] for n in dec_nrm)
        nsy = sum(n[1] for n in dec_nrm)
        nsz = sum(n[2] for n in dec_nrm)
        nl = math.sqrt(nsx * nsx + nsy * nsy + nsz * nsz) or 1.0
        nsx, nsy, nsz = nsx / nl, nsy / nl, nsz / nl
        min_dot = min((n[0] * nsx + n[1] * nsy + n[2] * nsz for n in dec_nrm), default=1.0)
        cone_sin = math.sqrt(max(0.0, 1.0 - min_dot * min_dot)) if min_dot > 0.0 else 1.0

        hdrs.append({
            "vtx_offset": m_i * CLUSTER_VTX,
            "idx_offset": idx_off,
            "vtx_cnt": len(gverts),
            "tri_cnt": len(lidx) // 3,
            "obj_id": 0,
            "flags": 0,
            "bsphere_f16": (f32_to_f16(cx), f32_to_f16(cy), f32_to_f16(cz), f32_to_f16(rad)),
            "cone_axis": (clamp_sym_i8(nsx), clamp_sym_i8(nsy), clamp_sym_i8(nsz)),
            "cone_cutoff": clamp_sym_i8(min(1.0, cone_sin + 0.05)),
            "aabb_scale_tag": 0,
        })

    return {
        "name": glb_path.stem[:60],
        "meshlet_cnt": len(meshlets),
        "hdrs": hdrs,
        "verts": verts,
        "local_idx": bytes(local_idx_stream),
        "world_scale": 1.0 / norm_scale,
    }


def parse_svg_huds(svg_path: Path) -> List[Dict[str, Any]]:
    tree = ET.parse(svg_path)
    root = tree.getroot()
    kind_map = {"panel": 0, "button": 1, "slider": 2, "text": 3, "vlist": 4}
    out: List[Dict[str, Any]] = []
    for elem in root.iter():
        tag = elem.tag.split("}")[-1]
        if tag != "g" or "data-kind" not in elem.attrib:
            continue
        hid = elem.attrib.get("id", f"hud_{len(out)}")
        kind = kind_map.get(elem.attrib["data-kind"], 0)
        bind_s = elem.attrib.get("data-bind", "0:0")
        bparts = bind_s.split(":")
        bind_obj = int(bparts[0]) if bparts and bparts[0].isdigit() else 0
        bind_slot = int(bparts[1]) if len(bparts) > 1 and bparts[1].isdigit() else 0

        rect_el = None
        text_el = None
        for ch in elem:
            ctag = ch.tag.split("}")[-1]
            if ctag == "rect":
                rect_el = ch
            elif ctag == "text":
                text_el = ch

        x = float(rect_el.attrib.get("x", "0")) if rect_el is not None else 0.0
        y = float(rect_el.attrib.get("y", "0")) if rect_el is not None else 0.0
        w = float(rect_el.attrib.get("width", "120")) if rect_el is not None else 120.0
        h = float(rect_el.attrib.get("height", "40")) if rect_el is not None else 40.0
        rx = float(rect_el.attrib.get("rx", "0")) if rect_el is not None else 0.0
        fill = parse_rgba(rect_el.attrib.get("fill", "#22344CEB") if rect_el is not None else "#22344CEB")
        stroke = parse_rgba(rect_el.attrib.get("stroke", "#4A6280AA") if rect_el is not None else "#4A6280AA", 0)
        label = (text_el.text or "").strip() if text_el is not None else ""
        out.append({
            "id": hid,
            "label": label,
            "kind": kind,
            "bounds_q14_2": (int(round(x * 4.0)), int(round(y * 4.0)), int(round(w * 4.0)), int(round(h * 4.0))),
            "fill_rgba8": fill,
            "border_rgba8": stroke,
            "bind_obj_idx": bind_obj,
            "radius_q6_2": max(0, min(255, int(round(rx * 4.0)))),
            "bind_dof_slot": bind_slot,
        })
    return out


def encode_bobr(tpls: List[Dict[str, Any]], objs: List[Dict[str, Any]], huds: List[Dict[str, Any]]) -> bytes:
    out = bytearray()
    out.extend(struct.pack("<IIIIII", MAGIC_ROW, FLAG_GEOM if tpls else 0, len(tpls), len(objs), 0, len(huds)))
    for t in tpls:
        nb = t["name"].encode("utf-8")
        out.extend(struct.pack("<HII", len(nb), t["meshlet_cnt"], len(t["local_idx"])))
        out.extend(nb)
        for h in t["hdrs"]:
            out.extend(struct.pack("<IIHHHH4H3bbI",
                                   h["vtx_offset"], h["idx_offset"], h["vtx_cnt"], h["tri_cnt"],
                                   h["obj_id"], h["flags"], *h["bsphere_f16"], *h["cone_axis"],
                                   h["cone_cutoff"], h["aabb_scale_tag"]))
        for v in t["verts"]:
            out.extend(struct.pack("<3H2b4B", *v))
        out.extend(t["local_idx"])
    for o in objs:
        out.extend(struct.pack("<3f4H3H3HI4HI", *o["pos"], *o["quat"], *o["scale"], *o["dof"], o["rgba8"], *o["clip"], o["flags"]))
        out.extend(struct.pack("<3H3HHH", *o["dof_min"], *o["dof_max"], o["template_id"], o["joint_mask"]))
    for hu in huds:
        ib = hu["id"].encode("utf-8")
        lb = hu["label"].encode("utf-8")
        out.extend(struct.pack("<4HIIHBBBBHHH",
                               *hu["bounds_q14_2"], hu["fill_rgba8"], hu["border_rgba8"],
                               hu["bind_obj_idx"], hu["kind"], hu["radius_q6_2"], hu["bind_dof_slot"],
                               0, len(ib), len(lb), 0))
    for o in objs:
        ib = o["id"].encode("utf-8")
        out.extend(struct.pack("<H", len(ib)))
        out.extend(ib)
    for hu in huds:
        out.extend(hu["id"].encode("utf-8"))
        out.extend(hu["label"].encode("utf-8"))
    return bytes(out)


def encode_bobc(tpls: List[Dict[str, Any]], objs: List[Dict[str, Any]], huds: List[Dict[str, Any]]) -> bytes:
    out = bytearray()
    out.extend(struct.pack("<IIIIII", MAGIC_COL, FLAG_GEOM if tpls else 0, len(tpls), len(objs), 0, len(huds)))
    for t in tpls:
        put_uvarint(out, t["meshlet_cnt"])
        put_uvarint(out, len(t["local_idx"]))
    if tpls:
        put_front_strings(out, [t["name"] for t in tpls])
    for t in tpls:
        pv = pi = po = 0
        for h in t["hdrs"]:
            put_uvarint(out, zz_enc(h["vtx_offset"] - pv)); pv = h["vtx_offset"]
            put_uvarint(out, zz_enc(h["idx_offset"] - pi)); pi = h["idx_offset"]
            put_uvarint(out, h["vtx_cnt"])
            put_uvarint(out, h["tri_cnt"])
            put_uvarint(out, zz_enc(h["obj_id"] - po)); po = h["obj_id"]
            put_uvarint(out, h["flags"])
            put_uvarint(out, h["aabb_scale_tag"])
        if t["hdrs"]:
            for k in range(4):
                put_u16_lanes(out, [h["bsphere_f16"][k] for h in t["hdrs"]])
            for k in range(3):
                for h in t["hdrs"]:
                    out.append(h["cone_axis"][k] & 0xFF)
            for h in t["hdrs"]:
                out.append(h["cone_cutoff"] & 0xFF)
        for k in range(3):
            prev = 0
            for v in t["verts"]:
                put_uvarint(out, zz_enc(v[k] - prev)); prev = v[k]
        for k in (3, 4):
            prev = 0
            for v in t["verts"]:
                put_uvarint(out, zz_enc(v[k] - prev)); prev = v[k]
        for k in (5, 6):
            prev = 0
            for v in t["verts"]:
                put_uvarint(out, zz_enc(v[k] - prev)); prev = v[k]
        for v in t["verts"]:
            out.append(v[7] & 0xFF)
        for v in t["verts"]:
            out.append(v[8] & 0xFF)
        pidx = 0
        for b in t["local_idx"]:
            put_uvarint(out, zz_enc(b - pidx)); pidx = b

    if objs:
        for k in range(3): put_f32_planes(out, [o["pos"][k] for o in objs])
        for k in range(4): put_u16_lanes(out, [o["quat"][k] for o in objs])
        for k in range(3): put_u16_lanes(out, [o["scale"][k] for o in objs])
        for k in range(3): put_u16_lanes(out, [o["dof"][k] for o in objs])
        for k in range(4): put_u16_lanes(out, [o["clip"][k] for o in objs])
        put_u32_lanes(out, [o["rgba8"] for o in objs])
        for o in objs: put_uvarint(out, o["flags"])
        for k in range(3): put_u16_lanes(out, [o["dof_min"][k] for o in objs])
        for k in range(3): put_u16_lanes(out, [o["dof_max"][k] for o in objs])
        for o in objs: put_uvarint(out, o["template_id"])
        for o in objs: put_uvarint(out, o["joint_mask"])

    if huds:
        for hu in huds: out.append(hu["kind"] & 0xFF)
        for k in range(4):
            prev = 0
            for hu in huds:
                cur = hu["bounds_q14_2"][k]
                put_uvarint(out, zz_enc(cur - prev)); prev = cur
        put_u32_lanes(out, [hu["fill_rgba8"] for hu in huds])
        put_u32_lanes(out, [hu["border_rgba8"] for hu in huds])
        for hu in huds: out.append(hu["radius_q6_2"] & 0xFF)
        prev_b = 0
        for hu in huds:
            cur = hu["bind_obj_idx"]
            put_uvarint(out, zz_enc(cur - prev_b)); prev_b = cur
        for hu in huds: out.append(hu["bind_dof_slot"] & 0xFF)

    if objs: put_front_strings(out, [o["id"] for o in objs])
    if huds:
        put_front_strings(out, [hu["id"] for hu in huds])
        put_front_strings(out, [hu["label"] for hu in huds])
    return bytes(out)


def write_sample_glb(path: Path) -> None:
    # Build minimal valid watertight tetrahedron/octahedron glTF 2.0 .glb
    pts = [(0.0, 1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, -1.0, 0.0)]
    tris = [(0, 2, 1), (0, 3, 2), (0, 4, 3), (0, 1, 4), (5, 1, 2), (5, 2, 3), (5, 3, 4), (5, 4, 1)]
    vbytes = b"".join(struct.pack("<fff", *p) for p in pts)
    ibytes = b"".join(struct.pack("<HHH", *t) for t in tris)
    bin_chunk = vbytes + ibytes
    while len(bin_chunk) % 4 != 0:
        bin_chunk += b"\x00"
    gltf = {
        "asset": {"version": "2.0"},
        "meshes": [{"name": "octa_crystal", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pts), "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": len(tris) * 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(vbytes)},
            {"buffer": 0, "byteOffset": len(vbytes), "byteLength": len(ibytes)},
        ],
        "buffers": [{"byteLength": len(bin_chunk)}],
    }
    jbytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(jbytes) % 4 != 0:
        jbytes += b" "
    total = 12 + 8 + len(jbytes) + 8 + len(bin_chunk)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, total) +
                     struct.pack("<II", len(jbytes), 0x4E4F534A) + jbytes +
                     struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk)


def main() -> None:
    ap = argparse.ArgumentParser(description="BLOOM build-time GLB/SVG -> BOBC/BOBR importer")
    sub = ap.add_subparsers(dest="cmd", required=True)

    imp = sub.add_parser("import", help="Compile .glb / .svg into .bobc or .bobr")
    imp.add_argument("--glb", action="append", type=Path, default=[])
    imp.add_argument("--svg", type=Path, default=None)
    imp.add_argument("--format", choices=("bobc", "bobr"), default="bobc")
    imp.add_argument("--out", type=Path, required=True)

    insp = sub.add_parser("inspect", help="Inspect header counts of .bobc / .bobr file")
    insp.add_argument("file", type=Path)

    samp = sub.add_parser("emit-sample-glb", help="Emit a valid minimal glTF 2.0 .glb for CI testing")
    samp.add_argument("out", type=Path)

    args = ap.parse_args()
    if args.cmd == "emit-sample-glb":
        write_sample_glb(args.out)
        print(f"Wrote sample GLB -> {args.out}")
        return

    if args.cmd == "inspect":
        d = args.file.read_bytes()
        magic, flags, tc, oc, ac, hc = struct.unpack_from("<IIIIII", d, 0)
        kind = "BOBC" if magic == MAGIC_COL else ("BOBR" if magic == MAGIC_ROW else "UNKNOWN")
        print(f"{args.file}: kind={kind} bytes={len(d)} geom={flags&1} tpls={tc} objs={oc} ana={ac} huds={hc}")
        return

    tpls: List[Dict[str, Any]] = []
    objs: List[Dict[str, Any]] = []
    for i, gp in enumerate(args.glb):
        t = parse_glb_to_template(gp, i)
        tpls.append(t)
        sc = f32_to_f16(t["world_scale"])
        objs.append({
            "id": f"imported_{t['name']}",
            "pos": (float(i) * 3.0, 0.0, 12.0),
            "quat": (0, 0, 0, f32_to_f16(1.0)),
            "scale": (sc, sc, sc),
            "dof": (0, 0, 0),
            "clip": (f32_to_f16(0.70703125), 0, f32_to_f16(0.70703125), 0),
            "rgba8": 0xFF40D8F0,
            "flags": 0,
            "dof_min": (f32_to_f16(-1.5), f32_to_f16(-1.5), f32_to_f16(-1.5)),
            "dof_max": (f32_to_f16(1.5), f32_to_f16(1.5), f32_to_f16(1.5)),
            "template_id": i,
            "joint_mask": 7,
        })
    huds = parse_svg_huds(args.svg) if args.svg else []
    blob = encode_bobc(tpls, objs, huds) if args.format == "bobc" else encode_bobr(tpls, objs, huds)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f"[bloom_import] {args.format.upper()} -> {args.out} ({len(blob)} B: tpls={len(tpls)} objs={len(objs)} huds={len(huds)})")


if __name__ == "__main__":
    main()
