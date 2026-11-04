#include "swept.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool H(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R) { bool h = false; SweptStatus s = swept_segment_sphere(p0, d, c, R, &h); CHECK(s == SWEPT_OK, "status"); return h; }

/* independent oracle: dense sampling in long double; returns minimum |p(t)-c|^2 - R^2 */
static long double dense_min(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R) {
    long double best = 1e300L; int N = 8192;
    for (int k = 0; k <= N; k++) {
        long double t = (long double)k / N;
        long double x = p0.x + t * d.x - c.x, y = p0.y + t * d.y - c.y, z = p0.z + t * d.z - c.z;
        long double f = x * x + y * y + z * z - (long double)R * R; if (f < best) best = f; }
    return best;
}
static int64_t rnd(int64_t lo, int64_t hi) { return lo + (int64_t)(((uint64_t)rand() << 31 ^ (uint64_t)rand()) % (uint64_t)(hi - lo + 1)); }

int main(void) {
    SweptV3 O = {0, 0, 0};
    /* exact boundary: sphere at (10,0,0) r=3, segment along y at x=0 passes at distance 10 -> miss; x offset test */
    CHECK(!H((SweptV3){7, -100, 0}, (SweptV3){0, 200, 0}, (SweptV3){10, 0, 0}, 2), "dist 3 > R=2 must miss");
    CHECK(H((SweptV3){7, -100, 0}, (SweptV3){0, 200, 0}, (SweptV3){10, 0, 0}, 3), "tangent dist==R must hit (inclusive)");
    CHECK(H((SweptV3){7, -100, 0}, (SweptV3){0, 200, 0}, (SweptV3){10, 0, 0}, 4), "dist 3 < R=4 must hit");
    /* tunneling: thin sphere r=500mm, step 86 m/60 = 1433 mm; start before, end after */
    CHECK(H((SweptV3){-700, 0, 0}, (SweptV3){1433, 0, 0}, O, 500) , "tunnel case caught");
    /* endpoint cases */
    CHECK(H(O, (SweptV3){10, 0, 0}, (SweptV3){-1, 0, 0}, 1), "start touches");
    CHECK(H(O, (SweptV3){10, 0, 0}, (SweptV3){11, 0, 0}, 1), "end touches");
    CHECK(!H(O, (SweptV3){10, 0, 0}, (SweptV3){12, 0, 0}, 1), "beyond end misses");
    CHECK(H(O, O, (SweptV3){3, 4, 0}, 5), "zero-length segment, on surface");
    CHECK(!H(O, O, (SweptV3){3, 4, 0}, 4), "zero-length segment, outside");
    /* range checks */
    bool h; CHECK(swept_segment_sphere(O, (SweptV3){(int64_t)1 << 21, 0, 0}, O, 1, &h) == SWEPT_ERR_RANGE, "step too large rejected");
    CHECK(swept_segment_sphere(O, O, (SweptV3){(int64_t)1 << 44, 0, 0}, 1, &h) == SWEPT_ERR_RANGE, "far centre rejected");
    /* large scale: centre at 1e12 mm (1e9 m), exact tangent via Pythagorean placement */
    {
        int64_t X = 1000000000000LL; /* segment from (X-3000, -1e5, 0) along +y, sphere at (X,0,0) R=3000 -> tangent */
        CHECK(H((SweptV3){X - 3000, -100000, 0}, (SweptV3){0, 200000, 0}, (SweptV3){X, 0, 0}, 3000), "1e9 m tangent hits");
        CHECK(!H((SweptV3){X - 3001, -100000, 0}, (SweptV3){0, 200000, 0}, (SweptV3){X, 0, 0}, 3000), "1e9 m 1mm outside misses");
    }
    /* entry parameter: sphere at x=100 r=10, segment (0..200): entry at x=90 -> t = 0.45 */
    { bool hh; uint32_t q; swept_entry_q16(O, (SweptV3){200, 0, 0}, (SweptV3){100, 0, 0}, 10, &hh, &q);
      CHECK(hh && q == (uint32_t)(0.45 * 65536), "entry q16 = %u expected %u", q, (unsigned)(0.45 * 65536)); }
    /* fuzz vs independent dense oracle */
    srand(12345); long mism_hit_missed = 0, mism_extra = 0, N = 200000;
    for (long i = 0; i < N; i++) {
        int64_t span = (i % 3 == 0) ? 20000 : (i % 3 == 1 ? 3000 : 400);
        SweptV3 p0 = {rnd(-span, span), rnd(-span, span), rnd(-span, span)};
        SweptV3 d  = {rnd(-1500, 1500), rnd(-1500, 1500), rnd(-1500, 1500)};
        SweptV3 c  = {rnd(-span, span), rnd(-span, span), rnd(-span, span)};
        int64_t R = rnd(0, span / 4 + 1);
        bool exact = H(p0, d, c, R); long double dm = dense_min(p0, d, c, R);
        long double dd = (long double)d.x * d.x + (long double)d.y * d.y + (long double)d.z * d.z;
        long double slack = dd / (8192.0L * 8192.0L); /* max error of dense sampling near the vertex */
        if (dm <= 0 && !exact) mism_hit_missed++;                 /* dense sees a hit, exact must too */
        if (exact && dm > slack + 1e-6L) mism_extra++;            /* exact hit must be within sampling slack */
    }
    CHECK(mism_hit_missed == 0, "exact missed %ld dense hits", mism_hit_missed);
    CHECK(mism_extra == 0, "exact hit beyond sampling slack in %ld cases", mism_extra);
    printf("fuzz: %ld cases, mismatches: missed=%ld extra=%ld\n", N, mism_hit_missed, mism_extra);
    printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails != 0;
}
