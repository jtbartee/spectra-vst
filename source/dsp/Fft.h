#pragma once
#include <cstddef>
#include <cmath>

namespace spectra {

/** In-place iterative radix-2 complex FFT; `n` must be a power of two. Direct port of
 *  js/fft.js, used for wavetable analysis (drawn shapes -> harmonics) and synthesis
 *  (harmonics -> band-limited mip frames). */
inline void fft (float* re, float* im, size_t n, bool inverse)
{
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t half = len >> 1;
        const double ang = ((inverse ? 2.0 : -2.0) * M_PI) / double (len);
        const double wr = std::cos (ang), wi = std::sin (ang);
        for (size_t i = 0; i < n; i += len)
        {
            double curR = 1.0, curI = 0.0;
            for (size_t k = 0; k < half; ++k)
            {
                const size_t a = i + k, b = i + k + half;
                const double vR = double (re[b]) * curR - double (im[b]) * curI;
                const double vI = double (re[b]) * curI + double (im[b]) * curR;
                re[b] = float (double (re[a]) - vR);
                im[b] = float (double (im[a]) - vI);
                re[a] = float (double (re[a]) + vR);
                im[a] = float (double (im[a]) + vI);
                const double nR = curR * wr - curI * wi;
                curI = curR * wi + curI * wr;
                curR = nR;
            }
        }
    }

    if (inverse)
        for (size_t i = 0; i < n; ++i) { re[i] /= float (n); im[i] /= float (n); }
}

} // namespace spectra
