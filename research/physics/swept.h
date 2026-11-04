#ifndef SWEPT_H
#define SWEPT_H
/* Exact integer swept-sphere tests. Units: millimetres, int64. No floating point.
 * Segment p(t)=p0+t*d, t in [0,1]; sphere centre c, radius R (R already includes the mover's radius,
 * i.e. the Minkowski sum). Preconditions (checked): |c-p0| < 2^43 mm, |d| < 2^20 mm, 0<=R<2^43 mm. */
#include <stdint.h>
#include <stdbool.h>

typedef struct { int64_t x, y, z; } SweptV3;

typedef enum { SWEPT_OK = 0, SWEPT_ERR_RANGE = -1 } SweptStatus;

/* hit: any point of the segment is within R of c. Exact (no rounding). */
SweptStatus swept_segment_sphere(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R, bool *hit);

/* Entry parameter as floor(t * 2^16) in [0, 65536]; valid only when the segment hits and the start
 * is outside the sphere (otherwise returns 0 for "already inside"). Integer sqrt, deterministic. */
SweptStatus swept_entry_q16(SweptV3 p0, SweptV3 d, SweptV3 c, int64_t R, bool *hit, uint32_t *t_q16);
#endif
