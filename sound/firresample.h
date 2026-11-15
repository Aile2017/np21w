/* Windowed-sinc FIR kernel shared by the streaming FM and offline PCM converters. */
#ifndef SOUND_FIRRESAMPLE_H
#define SOUND_FIRRESAMPLE_H

#include <math.h>

#define FIRRESAMPLE_PHASES 256
#define FIRRESAMPLE_TAPS   65

/* Each phase is normalized to unity DC gain in Q16. The caller owns the
 * phase table (phases * taps ints) and the scratch array (taps doubles). */
static void firresample_coefficients(int *coefficients, double *values,
                                     UINT taps, UINT phases, double outputRatio,
                                     double cutoffScale)
{
    const double pi = 3.14159265358979323846;
    const double cutoff = cutoffScale * ((outputRatio < 1.0) ? outputRatio : 1.0);
    const int center = taps / 2;
    UINT phase, tap;
    for (phase = 0; phase < phases; phase++) {
        const double fraction = (double)phase / phases;
        double total = 0;
        int sum = 0;
        int *row = coefficients + phase * taps;
        for (tap = 0; tap < taps; tap++) {
            const double distance = (double)tap + fraction - center;
            const double sinc = (fabs(distance) < 1e-12) ? 2.0 * cutoff :
                sin(2.0 * pi * cutoff * distance) / (pi * distance);
            const double window = 0.54 - 0.46 * cos(2.0 * pi * tap / (taps - 1));
            values[tap] = sinc * window;
            total += values[tap];
        }
        for (tap = 0; tap < taps; tap++) {
            const double scaled = values[tap] * 65536.0 / total;
            const int coefficient = (int)(scaled + ((scaled >= 0) ? 0.5 : -0.5));
            row[tap] = coefficient;
            sum += coefficient;
        }
        row[center] += 65536 - sum;
    }
}

static SINT32 firresample_round(SINT64 sample)
{
    return (SINT32)((sample >= 0 ? sample + 32768 : sample - 32768) / 65536);
}

#endif
