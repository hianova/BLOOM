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
from typing import Any, Dict, List, Optional, Sequence, Tuple

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
    count = acc["count"]
    ctype = acc["componentType"]
    atype = acc["type"]
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[atype]
    fmt_map = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
    ch = fmt_map[ctype]

    if "bufferView" in acc:
        bv = gltf["bufferViews"][acc["bufferView"]]
        base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
        elem_sz = struct.calcsize("<" + ch * ncomp)
        stride = bv.get("byteStride", elem_sz)
        out: List[Tuple[float, ...]] = [
            struct.unpack_from("<" + ch * ncomp, bin_blob, base + i * stride)
            for i in range(count)
        ]
    else:
        zero_val = 0.0 if ctype == 5126 else 0
        out = [(zero_val,) * ncomp for _ in range(count)]

    sparse = acc.get("sparse")
    if sparse:
        sp_cnt = int(sparse.get("count", 0))
        sp_idx_info = sparse["indices"]
        sp_val_info = sparse["values"]
        ibv = gltf["bufferViews"][sp_idx_info["bufferView"]]
        vbv = gltf["bufferViews"][sp_val_info["bufferView"]]
        ibase = ibv.get("byteOffset", 0) + sp_idx_info.get("byteOffset", 0)
        vbase = vbv.get("byteOffset", 0) + sp_val_info.get("byteOffset", 0)
        ich = fmt_map[sp_idx_info["componentType"]]
        isz = struct.calcsize("<" + ich)
        vsz = struct.calcsize("<" + ch * ncomp)
        istride = ibv.get("byteStride", isz)
        vstride = vbv.get("byteStride", vsz)
        for k in range(sp_cnt):
            idx = struct.unpack_from("<" + ich, bin_blob, ibase + k * istride)[0]
            if 0 <= idx < count:
                out[idx] = struct.unpack_from("<" + ch * ncomp, bin_blob, vbase + k * vstride)

    if acc.get("normalized", False):
        if ctype == 5120:
            out = [tuple(max(-1.0, v / 127.0) for v in item) for item in out]
        elif ctype == 5121:
            out = [tuple(v / 255.0 for v in item) for item in out]
        elif ctype == 5122:
            out = [tuple(max(-1.0, v / 32767.0) for v in item) for item in out]
        elif ctype == 5123:
            out = [tuple(v / 65535.0 for v in item) for item in out]
    return out


def _morton3d(x: float, y: float, z: float) -> int:
    def _part1(n: int) -> int:
        n &= 0x3FF
        n = (n ^ (n << 16)) & 0x030000FF
        n = (n ^ (n << 8)) & 0x0300F00F
        n = (n ^ (n << 4)) & 0x030C30C3
        n = (n ^ (n << 2)) & 0x09249249
        return n
    ix = max(0, min(1023, int(((x - LOCAL_BOX_LO) / LOCAL_BOX_SPAN) * 1023.0)))
    iy = max(0, min(1023, int(((y - LOCAL_BOX_LO) / LOCAL_BOX_SPAN) * 1023.0)))
    iz = max(0, min(1023, int(((z - LOCAL_BOX_LO) / LOCAL_BOX_SPAN) * 1023.0)))
    return _part1(ix) | (_part1(iy) << 1) | (_part1(iz) << 2)


def _quat_mul(a: Tuple[float, float, float, float], b: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def _quat_rot(q: Tuple[float, float, float, float], v: Tuple[float, float, float]) -> Tuple[float, float, float]:
    qx, qy, qz, qw = q
    vx, vy, vz = v
    tx = 2.0 * (qy * vz - qz * vy)
    ty = 2.0 * (qz * vx - qx * vz)
    tz = 2.0 * (qx * vy - qy * vx)
    return (
        vx + qw * tx + (qy * tz - qz * ty),
        vy + qw * ty + (qz * tx - qx * tz),
        vz + qw * tz + (qx * ty - qy * tx),
    )


def _mat3_to_quat(r00: float, r01: float, r02: float,
                  r10: float, r11: float, r12: float,
                  r20: float, r21: float, r22: float) -> Tuple[float, float, float, float]:
    tr = r00 + r11 + r22
    if tr > 0.0:
        s = math.sqrt(max(1e-12, tr + 1.0)) * 2.0
        qw = 0.25 * s
        qx = (r21 - r12) / s
        qy = (r02 - r20) / s
        qz = (r10 - r01) / s
    elif r00 > r11 and r00 > r22:
        s = math.sqrt(max(1e-12, 1.0 + r00 - r11 - r22)) * 2.0
        qw = (r21 - r12) / s
        qx = 0.25 * s
        qy = (r01 + r10) / s
        qz = (r02 + r20) / s
    elif r11 > r22:
        s = math.sqrt(max(1e-12, 1.0 + r11 - r00 - r22)) * 2.0
        qw = (r02 - r20) / s
        qx = (r01 + r10) / s
        qy = 0.25 * s
        qz = (r12 + r21) / s
    else:
        s = math.sqrt(max(1e-12, 1.0 + r22 - r00 - r11)) * 2.0
        qw = (r10 - r01) / s
        qx = (r02 + r20) / s
        qy = (r12 + r21) / s
        qz = 0.25 * s
    ql = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw) or 1.0
    return (qx / ql, qy / ql, qz / ql, qw / ql)


def _node_local_trs(node: Dict[str, Any]) -> Tuple[Tuple[float, float, float], Tuple[float, float, float, float], Tuple[float, float, float]]:
    if "matrix" in node and len(node["matrix"]) == 16:
        m = [float(v) for v in node["matrix"]]
        t = (m[12], m[13], m[14])
        sx = math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) or 1.0
        sy = math.sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]) or 1.0
        sz = math.sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]) or 1.0
        det = (m[0] * (m[5] * m[10] - m[6] * m[9]) -
               m[4] * (m[1] * m[10] - m[2] * m[9]) +
               m[8] * (m[1] * m[6] - m[2] * m[5]))
        if det < 0.0:
            sx = -sx
        r00, r10, r20 = m[0] / sx, m[1] / sx, m[2] / sx
        r01, r11, r21 = m[4] / sy, m[5] / sy, m[6] / sy
        r02, r12, r22 = m[8] / sz, m[9] / sz, m[10] / sz
        q = _mat3_to_quat(r00, r01, r02, r10, r11, r12, r20, r21, r22)
        return t, q, (sx, sy, sz)
    tv = node.get("translation", [0.0, 0.0, 0.0])
    rv = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    sv = node.get("scale", [1.0, 1.0, 1.0])
    ql = math.sqrt(rv[0] * rv[0] + rv[1] * rv[1] + rv[2] * rv[2] + rv[3] * rv[3]) or 1.0
    return (
        (float(tv[0]), float(tv[1]), float(tv[2])),
        (float(rv[0]) / ql, float(rv[1]) / ql, float(rv[2]) / ql, float(rv[3]) / ql),
        (float(sv[0]), float(sv[1]), float(sv[2])),
    )


def _bake_primitive(gltf: Dict[str, Any], bin_blob: bytes, prim: Dict[str, Any], tpl_idx: int, name: str,
                    axis_signs: Tuple[float, float, float] = (1.0, 1.0, 1.0)) -> Optional[Tuple[Dict[str, Any], int, int]]:
    mode = prim.get("mode", 4)
    if mode not in (4, 5, 6):
        return None
    attrs = prim["attributes"]
    positions = _read_accessor(gltf, bin_blob, attrs["POSITION"])
    if not positions:
        return None
    normals = _read_accessor(gltf, bin_blob, attrs["NORMAL"]) if "NORMAL" in attrs else None
    uvs = _read_accessor(gltf, bin_blob, attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None

    if "indices" in prim:
        seq = [int(x[0]) for x in _read_accessor(gltf, bin_blob, prim["indices"])]
    else:
        seq = list(range(len(positions)))

    raw_idx: List[int] = []
    if mode == 4:
        raw_idx = seq[: (len(seq) // 3) * 3]
    elif mode == 5:
        for i in range(len(seq) - 2):
            a, b, c = seq[i], seq[i + 1], seq[i + 2]
            if a == b or b == c or a == c:
                continue
            raw_idx.extend((a, b, c) if (i & 1) == 0 else (a, c, b))
    elif mode == 6:
        for i in range(1, len(seq) - 1):
            a, b, c = seq[0], seq[i], seq[i + 1]
            if a == b or b == c or a == c:
                continue
            raw_idx.extend((a, b, c))

    if len(raw_idx) < 3:
        return None

    sx_s, sy_s, sz_s = axis_signs
    flip_winding = (sx_s * sy_s * sz_s) < 0.0
    if flip_winding:
        for t in range(0, len(raw_idx) - 2, 3):
            raw_idx[t + 1], raw_idx[t + 2] = raw_idx[t + 2], raw_idx[t + 1]

    pos_signed = [(p[0] * sx_s, p[1] * sy_s, p[2] * sz_s) for p in positions]

    if normals is None:
        acc_n = [[0.0, 0.0, 0.0] for _ in range(len(pos_signed))]
        for t in range(0, len(raw_idx) - 2, 3):
            ia, ib, ic = raw_idx[t], raw_idx[t + 1], raw_idx[t + 2]
            pa, pb, pc = pos_signed[ia], pos_signed[ib], pos_signed[ic]
            e1 = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
            e2 = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
            nx = e1[1] * e2[2] - e1[2] * e2[1]
            ny = e1[2] * e2[0] - e1[0] * e2[2]
            nz = e1[0] * e2[1] - e1[1] * e2[0]
            for idx in (ia, ib, ic):
                acc_n[idx][0] += nx
                acc_n[idx][1] += ny
                acc_n[idx][2] += nz
        nrm_signed = []
        for n in acc_n:
            l = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) or 1.0
            nrm_signed.append((n[0] / l, n[1] / l, n[2] / l))
    else:
        nrm_signed = []
        for n in normals:
            nx, ny, nz = n[0] * sx_s, n[1] * sy_s, n[2] * sz_s
            l = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            nrm_signed.append((nx / l, ny / l, nz / l))

    max_abs = max((max(abs(p[0]), abs(p[1]), abs(p[2])) for p in pos_signed), default=1.0)
    norm_scale = (1.35 / max_abs) if (max_abs > 1e-6 and (max_abs > 1.45 or max_abs < 0.25)) else 1.0

    if uvs:
        us = [float(uv[0]) for uv in uvs]
        vs = [float(uv[1]) for uv in uvs]
        u_min, u_max = min(us), max(us)
        v_min, v_max = min(vs), max(vs)
        u_span = u_max - u_min
        v_span = v_max - v_min
        u_floor = math.floor(u_min)
        v_floor = math.floor(v_min)
    else:
        u_min = v_min = u_span = v_span = u_floor = v_floor = 0.0

    mat_idx = int(prim["material"]) if "material" in prim else -1
    double_sided = False
    rgba8 = 0xFF40D8F0
    if mat_idx >= 0 and "materials" in gltf and mat_idx < len(gltf["materials"]):
        mat = gltf["materials"][mat_idx]
        double_sided = bool(mat.get("doubleSided", False))
        bcf = mat.get("pbrMetallicRoughness", {}).get("baseColorFactor")
        if bcf and len(bcf) >= 4:
            r = max(0, min(255, int(round(bcf[0] * 255.0))))
            g = max(0, min(255, int(round(bcf[1] * 255.0))))
            b = max(0, min(255, int(round(bcf[2] * 255.0))))
            a = max(0, min(255, int(round(bcf[3] * 255.0))))
            rgba8 = (a << 24) | (b << 16) | (g << 8) | r

    # Morton-sort triangles by centroid for spatially coherent meshlets
    tris: List[Tuple[int, int, int, int]] = []
    for t in range(0, len(raw_idx) - 2, 3):
        ia, ib, ic = raw_idx[t], raw_idx[t + 1], raw_idx[t + 2]
        cx = (pos_signed[ia][0] + pos_signed[ib][0] + pos_signed[ic][0]) * (norm_scale / 3.0)
        cy = (pos_signed[ia][1] + pos_signed[ib][1] + pos_signed[ic][1]) * (norm_scale / 3.0)
        cz = (pos_signed[ia][2] + pos_signed[ib][2] + pos_signed[ic][2]) * (norm_scale / 3.0)
        tris.append((_morton3d(cx, cy, cz), ia, ib, ic))
    tris.sort(key=lambda item: item[0])

    meshlets: List[Tuple[List[int], List[int]]] = []
    cur_global_verts: List[int] = []
    cur_map: Dict[int, int] = {}
    cur_local_idx: List[int] = []
    for _, ia, ib, ic in tris:
        tri = (ia, ib, ic)
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
                px, py, pz = pos_signed[gi][0] * norm_scale, pos_signed[gi][1] * norm_scale, pos_signed[gi][2] * norm_scale
                nx, ny, nz = nrm_signed[gi]
                tu, tv = (uvs[gi][0], uvs[gi][1]) if uvs else (0.0, 0.0)
                if uvs:
                    tu = (tu - u_floor) if u_span <= 1.05 else ((tu - u_min) / max(1e-6, u_span))
                    tv = (tv - v_floor) if v_span <= 1.05 else ((tv - v_min) / max(1e-6, v_span))
                    tu = max(0.0, min(1.0, tu))
                    tv = max(0.0, min(1.0, tv))
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
        cone_c_val = 1.0 if double_sided else min(1.0, cone_sin + 0.05)

        hdrs.append({
            "vtx_offset": m_i * CLUSTER_VTX,
            "idx_offset": idx_off,
            "vtx_cnt": len(gverts),
            "tri_cnt": len(lidx) // 3,
            "obj_id": 0,
            "flags": 0,
            "bsphere_f16": (f32_to_f16(cx), f32_to_f16(cy), f32_to_f16(cz), f32_to_f16(rad)),
            "cone_axis": (clamp_sym_i8(nsx), clamp_sym_i8(nsy), clamp_sym_i8(nsz)),
            "cone_cutoff": clamp_sym_i8(cone_c_val),
            "aabb_scale_tag": 0,
        })

    tpl = {
        "name": name[:60],
        "meshlet_cnt": len(meshlets),
        "hdrs": hdrs,
        "verts": verts,
        "local_idx": bytes(local_idx_stream),
        "world_scale": 1.0 / norm_scale,
    }
    return tpl, rgba8, mat_idx


def export_pbr_manifest(gltf: Dict[str, Any], bin_blob: bytes, obj_mats: List[Dict[str, Any]], manifest_path: Path) -> None:
    tex_dir = manifest_path.parent / f"{manifest_path.stem}_textures"
    images = gltf.get("images", [])
    textures = gltf.get("textures", [])
    bvs = gltf.get("bufferViews", [])
    out_images: List[Dict[str, Any]] = []
    for i, img in enumerate(images):
        path_str = ""
        if "bufferView" in img:
            bv = bvs[int(img["bufferView"])]
            off = bv.get("byteOffset", 0)
            blen = bv.get("byteLength", 0)
            raw = bin_blob[off : off + blen]
            mime = img.get("mimeType", "image/png")
            ext = "jpg" if "jpeg" in mime or "jpg" in mime else "png"
            tex_dir.mkdir(parents=True, exist_ok=True)
            img_file = tex_dir / f"tex_{i}.{ext}"
            img_file.write_bytes(raw)
            path_str = str(img_file)
        out_images.append({"index": i, "path": path_str})

    def _tex_img_idx(tex_info: Any) -> int:
        if not isinstance(tex_info, dict) or "index" not in tex_info:
            return -1
        t_idx = int(tex_info["index"])
        if 0 <= t_idx < len(textures):
            return int(textures[t_idx].get("source", -1))
        return -1

    out_mats: List[Dict[str, Any]] = []
    for mi, m in enumerate(gltf.get("materials", [])):
        pmr = m.get("pbrMetallicRoughness", {})
        bcf = pmr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
        ef = m.get("emissiveFactor", [0.0, 0.0, 0.0])
        nrm = m.get("normalTexture", {})
        out_mats.append({
            "index": mi,
            "baseColorFactor": [float(v) for v in bcf],
            "metallicFactor": float(pmr.get("metallicFactor", 1.0)),
            "roughnessFactor": float(pmr.get("roughnessFactor", 1.0)),
            "emissiveFactor": [float(v) for v in ef],
            "doubleSided": bool(m.get("doubleSided", False)),
            "alphaMode": str(m.get("alphaMode", "OPAQUE")),
            "baseColorTex": _tex_img_idx(pmr.get("baseColorTexture")),
            "mrTex": _tex_img_idx(pmr.get("metallicRoughnessTexture")),
            "normalTex": _tex_img_idx(nrm),
            "emissiveTex": _tex_img_idx(m.get("emissiveTexture")),
            "aoTex": _tex_img_idx(m.get("occlusionTexture")),
            "normalScale": float(nrm.get("scale", 1.0)) if isinstance(nrm, dict) else 1.0,
        })

    manifest = {"textures": out_images, "materials": out_mats, "objects": obj_mats}
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def parse_glb_scene(glb_path: Path, tpl_offset: int = 0, x_offset: float = 0.0,
                    pbr_manifest: Optional[Path] = None, rh_to_lh: bool = False) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
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
        if ctype == 0x4E4F534A:
            json_blob = chunk
        elif ctype == 0x004E4942:
            bin_blob = chunk

    gltf = json.loads(json_blob.decode("utf-8"))
    meshes = gltf.get("meshes", [])
    tpls: List[Dict[str, Any]] = []
    prim_cache: Dict[Tuple[int, int, Tuple[int, int, int]], Optional[Tuple[int, float, int, int]]] = {}

    def _get_prim_tpl(m_idx: int, p_idx: int, ws: Tuple[float, float, float]) -> Optional[Tuple[int, float, int, int]]:
        sx_s = -1 if ws[0] < 0.0 else 1
        sy_s = -1 if ws[1] < 0.0 else 1
        sz_s = -1 if ws[2] < 0.0 else 1
        if rh_to_lh:
            sz_s = -sz_s
        signs = (sx_s, sy_s, sz_s)
        key = (m_idx, p_idx, signs)
        if key in prim_cache:
            return prim_cache[key]
        mesh_name = meshes[m_idx].get("name", f"{glb_path.stem}_{m_idx}")
        tname = mesh_name if p_idx == 0 else f"{mesh_name}_p{p_idx}"
        global_tid = tpl_offset + len(tpls)
        baked = _bake_primitive(gltf, bin_blob, meshes[m_idx]["primitives"][p_idx], global_tid, tname,
                                (float(sx_s), float(sy_s), float(sz_s)))
        if baked is None:
            prim_cache[key] = None
            return None
        tpl, rgba8, mat_idx = baked
        tpls.append(tpl)
        prim_cache[key] = (global_tid, tpl["world_scale"], rgba8, mat_idx)
        return prim_cache[key]

    objs: List[Dict[str, Any]] = []
    obj_mats: List[Dict[str, Any]] = []

    def _make_obj(oid: str, tid: int, pos3: Tuple[float, float, float], quat4: Tuple[float, float, float, float],
                  sc3: Tuple[float, float, float], rgba8: int) -> Dict[str, Any]:
        return {
            "id": oid[:60],
            "pos": pos3,
            "quat": (f32_to_f16(quat4[0]), f32_to_f16(quat4[1]), f32_to_f16(quat4[2]), f32_to_f16(quat4[3])),
            "scale": (f32_to_f16(sc3[0]), f32_to_f16(sc3[1]), f32_to_f16(sc3[2])),
            "dof": (0, 0, 0),
            "clip": (f32_to_f16(0.70703125), 0, f32_to_f16(0.70703125), 0),
            "rgba8": rgba8,
            "flags": 0,
            "dof_min": (f32_to_f16(-1.5), f32_to_f16(-1.5), f32_to_f16(-1.5)),
            "dof_max": (f32_to_f16(1.5), f32_to_f16(1.5), f32_to_f16(1.5)),
            "template_id": tid,
            "joint_mask": 7,
        }

    nodes = gltf.get("nodes")
    if nodes:
        scenes = gltf.get("scenes", [])
        if scenes:
            sc_idx = gltf.get("scene", 0)
            roots = scenes[sc_idx].get("nodes", [])
        else:
            children_set = {c for nd in nodes for c in nd.get("children", [])}
            roots = [idx for idx in range(len(nodes)) if idx not in children_set]

        def _visit(n_idx: int, pt: Tuple[float, float, float], pq: Tuple[float, float, float, float],
                   ps: Tuple[float, float, float]) -> None:
            nd = nodes[n_idx]
            lt, lq, ls = _node_local_trs(nd)
            scaled_lt = (ps[0] * lt[0], ps[1] * lt[1], ps[2] * lt[2])
            rl = _quat_rot(pq, scaled_lt)
            wt = (pt[0] + rl[0], pt[1] + rl[1], pt[2] + rl[2])
            wq = _quat_mul(pq, lq)
            ws = (ps[0] * ls[0], ps[1] * ls[1], ps[2] * ls[2])
            if "mesh" in nd:
                m_idx = int(nd["mesh"])
                prims = meshes[m_idx].get("primitives", [])
                nname = nd.get("name", f"{glb_path.stem}_n{n_idx}")
                for p_i in range(len(prims)):
                    info = _get_prim_tpl(m_idx, p_i, ws)
                    if info is None:
                        continue
                    tid, wscale, rgba8, mat_idx = info
                    oid = (nname if len(prims) == 1 else f"{nname}_p{p_i}")[:60]
                    abs_sc = (abs(ws[0]) * wscale, abs(ws[1]) * wscale, abs(ws[2]) * wscale)
                    ewt = (wt[0], wt[1], -wt[2]) if rh_to_lh else wt
                    ewq = (-wq[0], -wq[1], wq[2], wq[3]) if rh_to_lh else wq
                    objs.append(_make_obj(oid, tid, ewt, ewq, abs_sc, rgba8))
                    obj_mats.append({"id": oid, "material_index": mat_idx})
            for ch in nd.get("children", []):
                _visit(int(ch), wt, wq, ws)

        for r in roots:
            _visit(int(r), (x_offset, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0))
    else:
        for m_i, mesh in enumerate(meshes):
            for p_i in range(len(mesh.get("primitives", []))):
                info = _get_prim_tpl(m_i, p_i, (1.0, 1.0, 1.0))
                if info is None:
                    continue
                tid, wscale, rgba8, mat_idx = info
                mname = mesh.get("name", glb_path.stem)
                oid = (f"imported_{mname}" if len(meshes) == 1 and p_i == 0 else f"imported_{mname}_{m_i}_{p_i}")[:60]
                objs.append(_make_obj(oid, tid, (x_offset, 0.0, 12.0), (0.0, 0.0, 0.0, 1.0), (wscale, wscale, wscale), rgba8))
                obj_mats.append({"id": oid, "material_index": mat_idx})

    if pbr_manifest is not None:
        export_pbr_manifest(gltf, bin_blob, obj_mats, pbr_manifest)
    return tpls, objs


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


def write_multi_glb(path: Path) -> None:
    # 2 meshes, 2 PBR materials, 3 hierarchical parent-child nodes
    oct_pts = [(0.0, 1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, -1.0, 0.0)]
    oct_tris = [(0, 2, 1), (0, 3, 2), (0, 4, 3), (0, 1, 4), (5, 1, 2), (5, 2, 3), (5, 3, 4), (5, 4, 1)]
    box_pts = [(-0.6, -0.4, 0.6), (0.6, -0.4, 0.6), (0.6, 0.4, 0.6), (-0.6, 0.4, 0.6),
               (-0.6, -0.4, -0.6), (0.6, -0.4, -0.6), (0.6, 0.4, -0.6), (-0.6, 0.4, -0.6)]
    box_tris = [(0,1,2),(0,2,3),(5,4,7),(5,7,6),(1,5,6),(1,6,2),(4,0,3),(4,3,7),(3,2,6),(3,6,7),(4,5,1),(4,1,0)]
    v1 = b"".join(struct.pack("<fff", *p) for p in oct_pts)
    i1 = b"".join(struct.pack("<HHH", *t) for t in oct_tris)
    v2 = b"".join(struct.pack("<fff", *p) for p in box_pts)
    i2 = b"".join(struct.pack("<HHH", *t) for t in box_tris)
    bin_chunk = v1 + i1 + v2 + i2
    while len(bin_chunk) % 4 != 0:
        bin_chunk += b"\x00"
    o0 = 0
    o1 = len(v1)
    o2 = o1 + len(i1)
    o3 = o2 + len(v2)
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "hub_core", "mesh": 0, "translation": [0.0, 0.0, 8.0], "children": [1, 2]},
            {"name": "wing_left", "mesh": 1, "translation": [-2.0, 0.3, 0.0], "scale": [0.7, 0.7, 0.7]},
            {"name": "wing_right", "mesh": 0, "translation": [2.0, -0.2, 0.0], "scale": [0.55, 0.55, 0.55]},
        ],
        "materials": [
            {"pbrMetallicRoughness": {"baseColorFactor": [0.25, 0.85, 0.96, 1.0]}},
            {"pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.58, 0.22, 1.0]}},
        ],
        "meshes": [
            {"name": "octa_crystal", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]},
            {"name": "box_module", "primitives": [{"attributes": {"POSITION": 2}, "indices": 3, "material": 1}]},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(oct_pts), "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": len(oct_tris) * 3, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5126, "count": len(box_pts), "type": "VEC3"},
            {"bufferView": 3, "componentType": 5123, "count": len(box_tris) * 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": o0, "byteLength": len(v1)},
            {"buffer": 0, "byteOffset": o1, "byteLength": len(i1)},
            {"buffer": 0, "byteOffset": o2, "byteLength": len(v2)},
            {"buffer": 0, "byteOffset": o3, "byteLength": len(i2)},
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
    imp.add_argument("--pbr-manifest", type=Path, default=None)
    imp.add_argument("--rh-to-lh", action="store_true", help="Convert right-handed glTF +Z to BLOOM LH +Z forward")

    insp = sub.add_parser("inspect", help="Inspect header counts of .bobc / .bobr file")
    insp.add_argument("file", type=Path)

    samp = sub.add_parser("emit-sample-glb", help="Emit a valid minimal glTF 2.0 .glb for CI testing")
    samp.add_argument("out", type=Path)

    mult = sub.add_parser("emit-multi-glb", help="Emit a multi-mesh multi-node glTF 2.0 .glb for CI testing")
    mult.add_argument("out", type=Path)

    args = ap.parse_args()
    if args.cmd == "emit-sample-glb":
        write_sample_glb(args.out)
        print(f"Wrote sample GLB -> {args.out}")
        return

    if args.cmd == "emit-multi-glb":
        write_multi_glb(args.out)
        print(f"Wrote multi-node GLB -> {args.out}")
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
        st, so = parse_glb_scene(gp, len(tpls), float(i) * 4.0,
                                  args.pbr_manifest if (i == 0 and args.pbr_manifest) else None,
                                  rh_to_lh=args.rh_to_lh)
        tpls.extend(st)
        objs.extend(so)
    huds = parse_svg_huds(args.svg) if args.svg else []
    blob = encode_bobc(tpls, objs, huds) if args.format == "bobc" else encode_bobr(tpls, objs, huds)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f"[bloom_import] {args.format.upper()} -> {args.out} ({len(blob)} B: tpls={len(tpls)} objs={len(objs)} huds={len(huds)})")


if __name__ == "__main__":
    main()
