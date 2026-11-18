#pragma once
#include <cmath>

namespace ulz
{
/** Transposed direct form II biquad (RBJ cookbook coefficients). */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowPass (double sr, double hz, double q)  { set (sr, hz, q, false); }
    void setHighPass (double sr, double hz, double q) { set (sr, hz, q, true); }

private:
    void set (double sr, double hz, double q, bool highPass)
    {
        const double pi = 3.14159265358979323846;
        hz = std::fmin (hz, sr * 0.45);
        const double w0 = 2.0 * pi * hz / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        double nb0, nb1, nb2;
        if (highPass) { nb0 = (1.0 + cw) * 0.5; nb1 = -(1.0 + cw); nb2 = nb0; }
        else          { nb0 = (1.0 - cw) * 0.5; nb1 =  (1.0 - cw); nb2 = nb0; }
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - alpha) / a0);
    }
};
} // namespace ulz
