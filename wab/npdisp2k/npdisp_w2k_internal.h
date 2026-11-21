/**
 * @file	npdisp_w2k_internal.h
 * @brief	NPDISP Windows 2000 internal interface
 */

#pragma once

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#if 0
static void trace_fmt_exF(const char* fmt, ...)
{
	char stmp[2048];
	va_list ap;
	va_start(ap, fmt);
#if defined(_MSC_VER)
	_vsnprintf_s(stmp, sizeof(stmp), _TRUNCATE, fmt, ap);
#else
	vsnprintf(stmp, sizeof(stmp), fmt, ap);
#endif
	va_end(ap);
	OutputDebugStringA(stmp);
	OutputDebugStringA("\n");
}
#define	TRACEOUT11(s)	trace_fmt_exF s
#else
#define	TRACEOUT11(s)	(void)s
#endif	/* 1 */

int npdisp_w2k_guestRead(UINT32 vaddr, void *buffer, UINT32 size);
int npdisp_w2k_guestReadStrided(UINT32 vaddr, SINT32 srcStride, void *buffer, UINT32 dstStride, UINT32 rowBytes, UINT32 rows);
int npdisp_w2k_guestWrite(UINT32 vaddr, const void *buffer, UINT32 size);
UINT32 npdisp_w2k_hostCallBitBlt(UINT32 requestAddr);
UINT32 npdisp_w2k_hostCallOutput(UINT32 requestAddr);
UINT32 npdisp_w2k_hostCallGdi(UINT32 requestAddr);
UINT32 npdisp_w2k_hostCallText(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request);
UINT32 npdisp_w2k_hostCallStretch(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request);
void npdisp_w2k_chargePixels(UINT64 pixels);


#define NPDISP_W2K_OUTPUT_TRACE_GENERIC 0
#define NPDISP_W2K_OUTPUT_TRACE_RECT    1
#define NPDISP_W2K_OUTPUT_TRACE_ELLIPSE 2
#define NPDISP_W2K_OUTPUT_TRACE_MAX     3

#define NPDISP_W2K_BITBLT_TRACE_S2S  0
#define NPDISP_W2K_BITBLT_TRACE_M2S  1
#define NPDISP_W2K_BITBLT_TRACE_FILL 2
#define NPDISP_W2K_BITBLT_TRACE_MAX  3

#define NPDISP_W2K_STRETCH_MODE_BLACKONWHITE 0
#define NPDISP_W2K_STRETCH_MODE_WHITEONBLACK 1
#define NPDISP_W2K_STRETCH_MODE_COLORONCOLOR 2
#define NPDISP_W2K_STRETCH_MODE_HALFTONE     3
#define NPDISP_W2K_STRETCH_MODE_OTHER        4
#define NPDISP_W2K_STRETCH_MODE_MAX          5

#define NPDISP_W2K_STRETCH_REJECT_STATE   0
#define NPDISP_W2K_STRETCH_REJECT_SURFACE 1
#define NPDISP_W2K_STRETCH_REJECT_MASK    2
#define NPDISP_W2K_STRETCH_REJECT_MODE    3
#define NPDISP_W2K_STRETCH_REJECT_FORMAT  4
#define NPDISP_W2K_STRETCH_REJECT_ROP     5
#define NPDISP_W2K_STRETCH_REJECT_PATTERN 6
#define NPDISP_W2K_STRETCH_REJECT_XLATE   7
#define NPDISP_W2K_STRETCH_REJECT_RECT    8
#define NPDISP_W2K_STRETCH_REJECT_CLIP    9
#define NPDISP_W2K_STRETCH_REJECT_SOURCE  10
#define NPDISP_W2K_STRETCH_REJECT_MAX     11

void npdisp_w2k_traceReset(void);
void npdisp_w2k_traceGuestRead(UINT32 bytes, UINT32 clocks);
void npdisp_w2k_traceGuestWrite(UINT32 bytes, UINT32 clocks);
void npdisp_w2k_traceChargePixels(UINT64 pixels, UINT32 clocks);
void npdisp_w2k_traceHostCall(UINT32 command, UINT32 status);
void npdisp_w2k_traceGdi(UINT32 operation, UINT32 status);
void npdisp_w2k_traceStretchReject(UINT32 reason);
void npdisp_w2k_traceStretchMode(UINT32 mode);
void npdisp_w2k_traceBitBltClass(UINT32 type, UINT64 pixels, UINT64 readBytes, int patterned);
void npdisp_w2k_traceBitBltBatch(UINT64 calls, UINT64 bytes);
void npdisp_w2k_traceOutput(UINT32 type, UINT64 pixels);
void npdisp_w2k_traceFastRect(void);
void npdisp_w2k_traceFastEllipse(void);
void npdisp_w2k_traceTextDirect(void);

#endif
