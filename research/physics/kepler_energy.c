/* Verifies the note's claim: symplectic integration keeps orbital energy bounded, Euler does not.
 * Units: mu=1, semi-major axis a=1, period 2*pi. Numerical-analysis experiment (double precision). */
#include <stdio.h>
#include <math.h>
typedef struct { double x, y, vx, vy; } S;
static double energy(S s) { return 0.5 * (s.vx * s.vx + s.vy * s.vy) - 1.0 / sqrt(s.x * s.x + s.y * s.y); }
static void acc(S s, double *ax, double *ay) { double r2 = s.x * s.x + s.y * s.y, r3 = r2 * sqrt(r2); *ax = -s.x / r3; *ay = -s.y / r3; }
static S euler(S s, double dt) { double ax, ay; acc(s, &ax, &ay); S n = { s.x + dt * s.vx, s.y + dt * s.vy, s.vx + dt * ax, s.vy + dt * ay }; return n; }
static S sympl_euler(S s, double dt) { double ax, ay; acc(s, &ax, &ay); s.vx += dt * ax; s.vy += dt * ay; s.x += dt * s.vx; s.y += dt * s.vy; return s; }
static S verlet(S s, double dt) { double ax, ay; acc(s, &ax, &ay); s.vx += 0.5 * dt * ax; s.vy += 0.5 * dt * ay; s.x += dt * s.vx; s.y += dt * s.vy;
    acc(s, &ax, &ay); s.vx += 0.5 * dt * ax; s.vy += 0.5 * dt * ay; return s; }
typedef S (*Step)(S, double);
static void run(const char *name, Step f, double e, int steps_per_orbit, int orbits) {
    double rp = 1.0 - e, vp = sqrt((1.0 + e) / rp);        /* start at periapsis */
    S s = { rp, 0, 0, vp }; double E0 = energy(s), maxd = 0, dt = 2 * M_PI / steps_per_orbit; int bad = 0;
    for (long i = 0, n = (long)steps_per_orbit * orbits; i < n; i++) { s = f(s, dt);
        double d = fabs((energy(s) - E0) / E0); if (!(d == d) || d > 1e3) { bad = 1; break; } if (d > maxd) maxd = d; }
    double Ef = energy(s); printf("  %-16s max|dE/E|=%-10.3e final dE/E=%-10.3e%s\n", name, maxd, (Ef - E0) / E0, bad ? "  DIVERGED" : "");
}
int main(void) {
    struct { double e; int spo; int orbits; } cases[] = { {0.0, 100, 10000}, {0.5, 100, 10000}, {0.9, 100, 10000}, {0.9, 1000, 1000} };
    for (int i = 0; i < 4; i++) { printf("e=%.1f steps/orbit=%d orbits=%d\n", cases[i].e, cases[i].spo, cases[i].orbits);
        run("explicit Euler", euler, cases[i].e, cases[i].spo, cases[i].orbits);
        run("symplectic Euler", sympl_euler, cases[i].e, cases[i].spo, cases[i].orbits);
        run("velocity Verlet", verlet, cases[i].e, cases[i].spo, cases[i].orbits); }
    return 0; }
