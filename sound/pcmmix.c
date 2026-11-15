/**
 * @file	pcmmix.c
 * @brief	Implementation of the pcm sound
 */

#include "compiler.h"
#include "pcmmix.h"
#include "getsnd/getsnd.h"
#include "firresample.h"
#include "dosio.h"

BRESULT pcmmix_regist(PMIXDAT *dat, void *datptr, UINT datsize, UINT rate) {

	GETSND	gs;
	UINT8	tmp[256];
	UINT	size;
	UINT	r;
	SINT16	*buf;

	gs = getsnd_create(datptr, datsize);
	if (gs == NULL) {
		goto pmr_err1;
	}
	if (getsnd_setmixproc(gs, rate, 1) != SUCCESS) {
		goto pmr_err2;
	}
	size = 0;
	do {
		r = getsnd_getpcmbyleng(gs, tmp, sizeof(tmp));
		if (r > (UINT)-1 - size) goto pmr_err2;
		size += r;
	} while(r);
	getsnd_destroy(gs);
	if (size == 0) {
		goto pmr_err1;
	}

	buf = (SINT16 *)_MALLOC(size, "PCM DATA");
	if (buf == NULL) {
		goto pmr_err1;
	}
	gs = getsnd_create(datptr, datsize);
	if (gs == NULL) {
		_MFREE(buf);
		goto pmr_err1;
	}
	if (getsnd_setmixproc(gs, rate, 1) != SUCCESS) {
		_MFREE(buf);
		goto pmr_err2;
	}
	r = getsnd_getpcmbyleng(gs, buf, size);
	getsnd_destroy(gs);
	if (!r || r != size) {
		_MFREE(buf);
		goto pmr_err1;
	}
	dat->sample = buf;
	dat->samples = r / 2;
	return(SUCCESS);

pmr_err2:
	getsnd_destroy(gs);

pmr_err1:
	return(FAILURE);
}

#define PCM_FIR_TAPS 257
#define PCM_FIR_MAX_TAPS 4097

/* Offline conversion: decode at the native rate, then apply the centered
 * FIR. Unlike the live FM converter, the whole sample is available, so no
 * group delay is needed. Pad outside the sample with silence. */
static BRESULT pcmmix_regist_fir(PMIXDAT *dat, void *datptr, UINT datsize, UINT rate) {

    GETSND gs;
    PMIXDAT source;
    UINT inputRate, taps, i;
    UINT64 frames, position, index;
    int *coefficients;
    double *values;
    double cutoffScale = 0.49;
    SINT16 *output;
    BRESULT result = FAILURE;

    gs = getsnd_create(datptr, datsize);
    if (!gs) return FAILURE;
    inputRate = gs->samplingrate;
    getsnd_destroy(gs);
    if (!inputRate || !rate) return FAILURE;
    if (inputRate == rate) return pcmmix_regist(dat, datptr, datsize, rate);
    if (pcmmix_regist(&source, datptr, datsize, inputRate) != SUCCESS) return FAILURE;

    frames = ((UINT64)source.samples * rate + inputRate - 1) / inputRate;
    if (!frames || frames > (UINT)-1 / sizeof(SINT16)) goto fail_source;

    /* Retain approximately the same transition width relative to the
     * output rate when downsampling. Bound memory/work for extreme rates. */
    taps = PCM_FIR_TAPS;
    if (inputRate > rate) {
        UINT64 half = ((UINT64)(PCM_FIR_TAPS / 2) * inputRate + rate - 1) / rate;
        if (half > PCM_FIR_MAX_TAPS / 2) {
            half = PCM_FIR_MAX_TAPS / 2;
            /* With a capped kernel, leave more room for the transition band.
             * Do not narrow the passband beyond the original converter. */
            cutoffScale = 0.5 - 2.0 * inputRate / (rate * (double)PCM_FIR_MAX_TAPS);
            if (cutoffScale > 0.49) cutoffScale = 0.49;
            if (cutoffScale < 0.45) cutoffScale = 0.45;
        }
        taps = (UINT)(half * 2 + 1);
    }
    coefficients = (int *)_MALLOC(taps * FIRRESAMPLE_PHASES * sizeof(int), "PCM FIR coefficients");
    if (!coefficients) goto fail_source;
    values = (double *)_MALLOC(taps * sizeof(double), "PCM FIR scratch");
    if (!values) goto fail_coefficients;
    firresample_coefficients(coefficients, values, taps, FIRRESAMPLE_PHASES,
                             (double)rate / inputRate, cutoffScale);
    _MFREE(values);

    output = (SINT16 *)_MALLOC((UINT)frames * sizeof(SINT16), "PCM FIR data");
    if (!output) goto fail_coefficients;
    position = 0;
    for (i = 0; i < (UINT)frames; i++) {
        UINT tap;
        UINT64 phasePosition = (position % rate) * FIRRESAMPLE_PHASES;
        UINT phase = (UINT)(phasePosition / rate);
        UINT fraction = (UINT)((phasePosition % rate) * 65536 / rate);
        UINT nextPhase = (phase + 1) % FIRRESAMPLE_PHASES;
        const int *row = coefficients + phase * taps;
        const int *nextRow = coefficients + nextPhase * taps;
        SINT64 sum = 0, nextSum = 0;
        UINT64 nextIndex;
        SINT32 sample;
        index = position / rate + taps / 2;
        nextIndex = index + (nextPhase == 0);
        for (tap = 0; tap < taps; tap++) {
            if (index >= tap && index - tap < source.samples)
                sum += (SINT64)source.sample[(UINT)(index - tap)] * row[tap];
            if (fraction && nextIndex >= tap && nextIndex - tap < source.samples)
                nextSum += (SINT64)source.sample[(UINT)(nextIndex - tap)] * nextRow[tap];
        }
        /* Interpolate neighboring phases; wrap the final phase to the
         * first phase at the next input sample. */
        if (fraction) sum += (nextSum - sum) * fraction / 65536;
        sample = firresample_round(sum);
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        output[i] = (SINT16)sample;
        position += inputRate;
    }
    dat->sample = output;
    dat->samples = (UINT)frames;
    result = SUCCESS;

fail_coefficients:
    _MFREE(coefficients);
fail_source:
    _MFREE(source.sample);
    return result;
}

BRESULT pcmmix_regfile(PMIXDAT *dat, const OEMCHAR *fname, UINT rate) {

	FILEH	fh;
	FILELEN	len;
	UINT	size;
	UINT8	*ptr;
	BRESULT	r;

	r = FAILURE;
	fh = file_open_rb(fname);
	if (fh == FILEH_INVALID) {
		goto pmrf_err1;
	}
	len = file_getsize(fh);
	if (len <= 0 || (UINT64)len > (UINT)-1) {
		goto pmrf_err2;
	}
	size = (UINT)len;
	ptr = (UINT8 *)_MALLOC(size, fname);
	if (ptr == NULL) {
		goto pmrf_err2;
	}
	if (file_read(fh, ptr, size) == size) {
		r = pcmmix_regist_fir(dat, ptr, size, rate);
	}
	file_close(fh);
	_MFREE(ptr);
	return(r);

pmrf_err2:
	file_close(fh);

pmrf_err1:
	return(FAILURE);
}

void SOUNDCALL pcmmix_getpcm(PCMMIX hdl, SINT32 *pcm, UINT count) {

	UINT32		bitmap;
	PMIXTRK		*t;
const SINT16	*s;
	UINT		srem;
	SINT32		*d;
	UINT		drem;
	UINT		r;
	UINT		j;
	UINT		flag;
	SINT32		vol;
	SINT32		samp;

	if ((hdl->hdr.playing == 0) || (count == 0))  {
		return;
	}
	t = hdl->trk;
	bitmap = 1;
	do {
		if (hdl->hdr.playing & bitmap) {
			s = t->pcm;
			srem = t->remain;
			d = pcm;
			drem = count;
			flag = t->flag;
			vol = t->volume;
			do {
				r = min(srem, drem);
				switch(flag & (PMIXFLAG_L | PMIXFLAG_R)) {
					case PMIXFLAG_L:
						for (j=0; j<r; j++) {
							d[j*2+0] += (s[j] * vol) >> 12;
						}
						break;

					case PMIXFLAG_R:
						for (j=0; j<r; j++) {
							d[j*2+1] += (s[j] * vol) >> 12;
						}
						break;

					case PMIXFLAG_L | PMIXFLAG_R:
						for (j=0; j<r; j++) {
							samp = (s[j] * vol) >> 12;
							d[j*2+0] += samp;
							d[j*2+1] += samp;
						}
						break;
				}
				s += r;
				d += r*2;
				srem -= r;
				if (srem == 0) {
					if (flag & PMIXFLAG_LOOP) {
						s = t->data.sample;
						srem = t->data.samples;
					}
					else {
						hdl->hdr.playing &= ~bitmap;
						break;
					}
				}
				drem -= r;
			} while(drem);
			t->pcm = s;
			t->remain = srem;
		}
		t++;
		bitmap <<= 1;
	} while(bitmap < hdl->hdr.enable);
}
