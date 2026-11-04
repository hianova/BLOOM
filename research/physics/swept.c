#include "swept.h"

__extension__ typedef __int128 i128;
__extension__ typedef unsigned __int128 u128;

#define LIM_POS ((int64_t)1 << 43)
#define LIM_STEP ((int64_t)1 << 20)

static i128 dot(SweptV3 a, SweptV3 b) { return (i128)a.x * b.x + (i128)a.y * b.y + (i128)a.z * b.z; }
static SweptV3 sub(SweptV3 a, SweptV3 b) { return (SweptV3){a.x - b.x, a.y - b.y, a.z - b.z}; }

static bool in_range(SweptV3 v, int64_t lim) {
    int64_t m = lim;
    return v.x > -m && v.x < m && v.y > -m && v.y < m && v.z > -m && v.z < m;
}

static SweptStatus check(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R) {
    /* componentwise bound; the squared norms then stay far below 2^127 for the products used. */
    SweptV3 u = sub(c, p0);
    if (R < 0 || R >= LIM_POS || !in_range(u, LIM_POS) || !in_range(d, LIM_STEP)) return SWEPT_ERR_RANGE;
    return SWEPT_OK;
}

SweptStatus swept_segment_sphere(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R, bool *hit) {
    SweptStatus st = check(p0, d, c, R);
    if (st != SWEPT_OK) return st;
    SweptV3 u = sub(c, p0);
    i128 uu = dot(u, u), ud = dot(u, d), dd = dot(d, d), RR = (i128)R * R;
    if (dd == 0 || ud <= 0) { *hit = uu <= RR; return SWEPT_OK; }          /* closest point is p0 */
    if (ud >= dd) { SweptV3 w = sub(u, d); *hit = dot(w, w) <= RR; return SWEPT_OK; } /* closest is p1 */
    /* interior: perpendicular distance^2 = (uu*dd - ud^2)/dd  <=  R^2  <=>  uu*dd - ud^2 <= R^2*dd */
    *hit = (uu * dd - ud * ud) <= RR * dd;
    return SWEPT_OK;
}

static uint64_t isqrt_u128(u128 n) {
    if (n == 0) return 0;
    u128 x = n, y = (x + 1) / 2;
    /* start from a power of two above sqrt(n) to guarantee monotone convergence */
    int bits = 0; for (u128 t = n; t; t >>= 1) bits++;
    x = (u128)1 << ((bits + 1) / 2);
    y = (x + n / x) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return (uint64_t)x;
}

SweptStatus swept_entry_q16(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R, bool *hit, uint32_t *t_q16) {
    SweptStatus st = check(p0, d, c, R);
    if (st != SWEPT_OK) return st;
    bool h; swept_segment_sphere(p0, d, c, R, &h);
    *hit = h; *t_q16 = 0;
    if (!h) return SWEPT_OK;
    SweptV3 u = sub(c, p0);
    i128 uu = dot(u, u), ud = dot(u, d), dd = dot(d, d), RR = (i128)R * R;
    if (uu <= RR || dd == 0) return SWEPT_OK;                               /* starts inside */
    /* |u - t d|^2 = R^2 -> t = (ud - sqrt(ud^2 - dd*(uu-RR))) / dd ; disc >= 0 whenever hit */
    i128 disc = ud * ud - dd * (uu - RR);
    if (disc < 0) disc = 0;
    uint64_t s = isqrt_u128((u128)disc);
    i128 num = ud - (i128)s;                                                /* may be slightly <0 by flooring */
    if (num < 0) num = 0;
    i128 q = (num << 16) / dd;
    if (q > 65536) q = 65536;
    *t_q16 = (uint32_t)q;
    return SWEPT_OK;
}
