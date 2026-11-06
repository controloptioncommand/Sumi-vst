#pragma once
#include <cmath>
#include <algorithm>

namespace sumi
{
// Частотная шкала картинки: положение 0…1 между Low и High (0 — низ картинки, 1 — верх).
// Lin — равные герцы, Log — равные октавы, Mel — слуховая шкала (как у многих анализаторов).
struct FreqMap
{
    int scale = 1;   // 0 Lin, 1 Log, 2 Mel
    double lo = 200.0, hi = 16000.0;

    FreqMap() = default;
    FreqMap (int s, double l, double h) : scale (s), lo (l), hi (h) { if (hi <= lo * 1.01) hi = lo * 1.01; }

    static double warp (int s, double f)
    {
        switch (s)
        {
            case 0:  return f;
            case 2:  return 2595.0 * std::log10 (1.0 + std::max (0.0, f) / 700.0);
            default: return std::log (std::max (f, 1.0));
        }
    }
    static double unwarp (int s, double w)
    {
        switch (s)
        {
            case 0:  return w;
            case 2:  return 700.0 * (std::pow (10.0, w / 2595.0) - 1.0);
            default: return std::exp (w);
        }
    }

    double pos (double f) const { return (warp (scale, f) - warp (scale, lo)) / (warp (scale, hi) - warp (scale, lo)); }
    double freq (double p) const { return unwarp (scale, warp (scale, lo) + p * (warp (scale, hi) - warp (scale, lo))); }
};
} // namespace sumi
