/**
 * @file	npdisp_w2k_trace.cpp
 * @brief	NPDISP Windows 2000 performance diagnostics
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>

#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"

#define NPDISP_W2K_TRACE_INTERVAL 4096ULL
#if defined(_MSC_VER)
#define NPDISP_W2K_TRACE_U64 "%I64u"
#else
#define NPDISP_W2K_TRACE_U64 "%llu"
#endif

#if 0
#undef	TRACEOUT
static void trace_fmt_ex(const char* fmt, ...)
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
#define	TRACEOUT(s)	trace_fmt_ex s
#endif

typedef struct {
	UINT64 hostCalls;
	UINT64 ok;
	UINT64 unsupported;
	UINT64 badRequest;
	UINT64 bitbltCalls;
	UINT64 bitbltOk;
	UINT64 bitbltClass[NPDISP_W2K_BITBLT_TRACE_MAX];
	UINT64 bitbltClassPixels[NPDISP_W2K_BITBLT_TRACE_MAX];
	UINT64 bitbltMemoryReadBytes;
	UINT64 bitbltPattern;
	UINT64 bitbltBatchCalls;
	UINT64 bitbltBatchBytes;
	UINT64 outputCalls;
	UINT64 outputOk;
	UINT64 outputClass[NPDISP_W2K_OUTPUT_TRACE_MAX];
	UINT64 outputClassPixels[NPDISP_W2K_OUTPUT_TRACE_MAX];
	UINT64 fastRectCalls;
	UINT64 fastEllipseCalls;
	UINT64 gdiCalls;
	UINT64 gdiOk;
	UINT64 textCalls;
	UINT64 textOk;
	UINT64 textDirectCalls;
	UINT64 stretchCalls;
	UINT64 stretchOk;
	UINT64 stretchRopCalls;
	UINT64 stretchRopOk;
	UINT64 guestReadBytes;
	UINT64 guestWriteBytes;
	UINT64 pixelWork;
	UINT64 memoryClocks;
	UINT64 rasterClocks;
	UINT64 chargedClocks;
	UINT64 stretchReject[NPDISP_W2K_STRETCH_REJECT_MAX];
	UINT64 stretchMode[NPDISP_W2K_STRETCH_MODE_MAX];
} NPDISP_W2K_TRACE_STATS;

static NPDISP_W2K_TRACE_STATS npdisp_w2k_traceStats;

static void npdisp_w2k_traceDump(void)
{
	TRACEOUT(("NPDISP W2K PROF calls=" NPDISP_W2K_TRACE_U64 " ok=" NPDISP_W2K_TRACE_U64 " unsupported=" NPDISP_W2K_TRACE_U64 " bad=" NPDISP_W2K_TRACE_U64 " bitblt=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " output=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " gdi=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " read=" NPDISP_W2K_TRACE_U64 " write=" NPDISP_W2K_TRACE_U64 " pixels=" NPDISP_W2K_TRACE_U64 " memclk=" NPDISP_W2K_TRACE_U64 " rasterclk=" NPDISP_W2K_TRACE_U64 " clocks=" NPDISP_W2K_TRACE_U64,
		(unsigned long long)npdisp_w2k_traceStats.hostCalls,
		(unsigned long long)npdisp_w2k_traceStats.ok,
		(unsigned long long)npdisp_w2k_traceStats.unsupported,
		(unsigned long long)npdisp_w2k_traceStats.badRequest,
		(unsigned long long)npdisp_w2k_traceStats.bitbltOk,
		(unsigned long long)npdisp_w2k_traceStats.bitbltCalls,
		(unsigned long long)npdisp_w2k_traceStats.outputOk,
		(unsigned long long)npdisp_w2k_traceStats.outputCalls,
		(unsigned long long)npdisp_w2k_traceStats.gdiOk,
		(unsigned long long)npdisp_w2k_traceStats.gdiCalls,
		(unsigned long long)npdisp_w2k_traceStats.guestReadBytes,
		(unsigned long long)npdisp_w2k_traceStats.guestWriteBytes,
		(unsigned long long)npdisp_w2k_traceStats.pixelWork,
		(unsigned long long)npdisp_w2k_traceStats.memoryClocks,
		(unsigned long long)npdisp_w2k_traceStats.rasterClocks,
		(unsigned long long)npdisp_w2k_traceStats.chargedClocks));
	TRACEOUT(("NPDISP W2K BLT s2s=" NPDISP_W2K_TRACE_U64 " m2s=" NPDISP_W2K_TRACE_U64 " fill=" NPDISP_W2K_TRACE_U64 " pattern=" NPDISP_W2K_TRACE_U64 " m2sread=" NPDISP_W2K_TRACE_U64 " batch=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " s2spx=" NPDISP_W2K_TRACE_U64 " m2spx=" NPDISP_W2K_TRACE_U64 " fillpx=" NPDISP_W2K_TRACE_U64,
		(unsigned long long)npdisp_w2k_traceStats.bitbltClass[NPDISP_W2K_BITBLT_TRACE_S2S],
		(unsigned long long)npdisp_w2k_traceStats.bitbltClass[NPDISP_W2K_BITBLT_TRACE_M2S],
		(unsigned long long)npdisp_w2k_traceStats.bitbltClass[NPDISP_W2K_BITBLT_TRACE_FILL],
		(unsigned long long)npdisp_w2k_traceStats.bitbltPattern,
		(unsigned long long)npdisp_w2k_traceStats.bitbltMemoryReadBytes,
		(unsigned long long)npdisp_w2k_traceStats.bitbltBatchCalls,
		(unsigned long long)npdisp_w2k_traceStats.bitbltBatchBytes,
		(unsigned long long)npdisp_w2k_traceStats.bitbltClassPixels[NPDISP_W2K_BITBLT_TRACE_S2S],
		(unsigned long long)npdisp_w2k_traceStats.bitbltClassPixels[NPDISP_W2K_BITBLT_TRACE_M2S],
		(unsigned long long)npdisp_w2k_traceStats.bitbltClassPixels[NPDISP_W2K_BITBLT_TRACE_FILL]));
	TRACEOUT(("NPDISP W2K OUT generic=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " rect=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " ellipse=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " fastrect=" NPDISP_W2K_TRACE_U64 " fastellipse=" NPDISP_W2K_TRACE_U64,
		(unsigned long long)npdisp_w2k_traceStats.outputClass[NPDISP_W2K_OUTPUT_TRACE_GENERIC],
		(unsigned long long)npdisp_w2k_traceStats.outputClassPixels[NPDISP_W2K_OUTPUT_TRACE_GENERIC],
		(unsigned long long)npdisp_w2k_traceStats.outputClass[NPDISP_W2K_OUTPUT_TRACE_RECT],
		(unsigned long long)npdisp_w2k_traceStats.outputClassPixels[NPDISP_W2K_OUTPUT_TRACE_RECT],
		(unsigned long long)npdisp_w2k_traceStats.outputClass[NPDISP_W2K_OUTPUT_TRACE_ELLIPSE],
		(unsigned long long)npdisp_w2k_traceStats.outputClassPixels[NPDISP_W2K_OUTPUT_TRACE_ELLIPSE],
		(unsigned long long)npdisp_w2k_traceStats.fastRectCalls,
		(unsigned long long)npdisp_w2k_traceStats.fastEllipseCalls));
	TRACEOUT(("NPDISP W2K GDI text=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " direct=" NPDISP_W2K_TRACE_U64 " stretch=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " stretchrop=" NPDISP_W2K_TRACE_U64 "/" NPDISP_W2K_TRACE_U64 " reject state=" NPDISP_W2K_TRACE_U64 " surface=" NPDISP_W2K_TRACE_U64 " mask=" NPDISP_W2K_TRACE_U64 " mode=" NPDISP_W2K_TRACE_U64 " format=" NPDISP_W2K_TRACE_U64 " rop=" NPDISP_W2K_TRACE_U64 " pattern=" NPDISP_W2K_TRACE_U64 " xlate=" NPDISP_W2K_TRACE_U64 " rect=" NPDISP_W2K_TRACE_U64 " clip=" NPDISP_W2K_TRACE_U64 " source=" NPDISP_W2K_TRACE_U64,
		(unsigned long long)npdisp_w2k_traceStats.textOk,
		(unsigned long long)npdisp_w2k_traceStats.textCalls,
		(unsigned long long)npdisp_w2k_traceStats.textDirectCalls,
		(unsigned long long)npdisp_w2k_traceStats.stretchOk,
		(unsigned long long)npdisp_w2k_traceStats.stretchCalls,
		(unsigned long long)npdisp_w2k_traceStats.stretchRopOk,
		(unsigned long long)npdisp_w2k_traceStats.stretchRopCalls,
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_STATE],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_SURFACE],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_MASK],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_MODE],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_FORMAT],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_ROP],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_PATTERN],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_XLATE],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_RECT],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_CLIP],
		(unsigned long long)npdisp_w2k_traceStats.stretchReject[NPDISP_W2K_STRETCH_REJECT_SOURCE]));
	TRACEOUT(("NPDISP W2K STRETCH mode black=" NPDISP_W2K_TRACE_U64 " white=" NPDISP_W2K_TRACE_U64 " color=" NPDISP_W2K_TRACE_U64 " halftone=" NPDISP_W2K_TRACE_U64 " other=" NPDISP_W2K_TRACE_U64,
		(unsigned long long)npdisp_w2k_traceStats.stretchMode[NPDISP_W2K_STRETCH_MODE_BLACKONWHITE],
		(unsigned long long)npdisp_w2k_traceStats.stretchMode[NPDISP_W2K_STRETCH_MODE_WHITEONBLACK],
		(unsigned long long)npdisp_w2k_traceStats.stretchMode[NPDISP_W2K_STRETCH_MODE_COLORONCOLOR],
		(unsigned long long)npdisp_w2k_traceStats.stretchMode[NPDISP_W2K_STRETCH_MODE_HALFTONE],
		(unsigned long long)npdisp_w2k_traceStats.stretchMode[NPDISP_W2K_STRETCH_MODE_OTHER]));
}

void npdisp_w2k_traceReset(void)
{
	memset(&npdisp_w2k_traceStats, 0, sizeof(npdisp_w2k_traceStats));
}

void npdisp_w2k_traceGuestRead(UINT32 bytes, UINT32 clocks)
{
	npdisp_w2k_traceStats.guestReadBytes += bytes;
	npdisp_w2k_traceStats.memoryClocks += clocks;
	npdisp_w2k_traceStats.chargedClocks += clocks;
}

void npdisp_w2k_traceGuestWrite(UINT32 bytes, UINT32 clocks)
{
	npdisp_w2k_traceStats.guestWriteBytes += bytes;
	npdisp_w2k_traceStats.memoryClocks += clocks;
	npdisp_w2k_traceStats.chargedClocks += clocks;
}

void npdisp_w2k_traceChargePixels(UINT64 pixels, UINT32 clocks)
{
	npdisp_w2k_traceStats.pixelWork += pixels;
	npdisp_w2k_traceStats.rasterClocks += clocks;
	npdisp_w2k_traceStats.chargedClocks += clocks;
}

void npdisp_w2k_traceHostCall(UINT32 command, UINT32 status)
{
	npdisp_w2k_traceStats.hostCalls++;
	if (status == NPDISP_W2K_HOSTCALL_OK) npdisp_w2k_traceStats.ok++;
	else if (status == NPDISP_W2K_HOSTCALL_UNSUPPORTED) npdisp_w2k_traceStats.unsupported++;
	else if (status == NPDISP_W2K_HOSTCALL_BAD_REQUEST) npdisp_w2k_traceStats.badRequest++;
	if (command == NPDISP_W2K_HOSTCALL_BITBLT) {
		npdisp_w2k_traceStats.bitbltCalls++;
		if (status == NPDISP_W2K_HOSTCALL_OK) npdisp_w2k_traceStats.bitbltOk++;
	}
	else if (command == NPDISP_W2K_HOSTCALL_OUTPUT) {
		npdisp_w2k_traceStats.outputCalls++;
		if (status == NPDISP_W2K_HOSTCALL_OK) npdisp_w2k_traceStats.outputOk++;
	}
	else if (command == NPDISP_W2K_HOSTCALL_GDI) {
		npdisp_w2k_traceStats.gdiCalls++;
		if (status == NPDISP_W2K_HOSTCALL_OK) npdisp_w2k_traceStats.gdiOk++;
	}
	if ((npdisp_w2k_traceStats.hostCalls & (NPDISP_W2K_TRACE_INTERVAL - 1)) == 0) npdisp_w2k_traceDump();
}

void npdisp_w2k_traceGdi(UINT32 operation, UINT32 status)
{
	UINT64 *calls = NULL;
	UINT64 *ok = NULL;

	if (operation == NPDISP_W2K_GDI_TEXT_OUT) {
		calls = &npdisp_w2k_traceStats.textCalls;
		ok = &npdisp_w2k_traceStats.textOk;
	}
	else if (operation == NPDISP_W2K_GDI_STRETCH_BLT) {
		calls = &npdisp_w2k_traceStats.stretchCalls;
		ok = &npdisp_w2k_traceStats.stretchOk;
	}
	else if (operation == NPDISP_W2K_GDI_STRETCH_BLT_ROP) {
		calls = &npdisp_w2k_traceStats.stretchRopCalls;
		ok = &npdisp_w2k_traceStats.stretchRopOk;
	}
	if (calls) {
		(*calls)++;
		if (status == NPDISP_W2K_HOSTCALL_OK && ok) (*ok)++;
	}
}

void npdisp_w2k_traceStretchReject(UINT32 reason)
{
	if (reason < NPDISP_W2K_STRETCH_REJECT_MAX) npdisp_w2k_traceStats.stretchReject[reason]++;
}

void npdisp_w2k_traceStretchMode(UINT32 mode)
{
	UINT32 index;
	if (mode == 1) index = NPDISP_W2K_STRETCH_MODE_BLACKONWHITE;
	else if (mode == 2) index = NPDISP_W2K_STRETCH_MODE_WHITEONBLACK;
	else if (mode == 3) index = NPDISP_W2K_STRETCH_MODE_COLORONCOLOR;
	else if (mode == 4) index = NPDISP_W2K_STRETCH_MODE_HALFTONE;
	else index = NPDISP_W2K_STRETCH_MODE_OTHER;
	npdisp_w2k_traceStats.stretchMode[index]++;
}

void npdisp_w2k_traceBitBltClass(UINT32 type, UINT64 pixels, UINT64 readBytes, int patterned)
{
	if (type < NPDISP_W2K_BITBLT_TRACE_MAX) {
		npdisp_w2k_traceStats.bitbltClass[type]++;
		npdisp_w2k_traceStats.bitbltClassPixels[type] += pixels;
	}
	npdisp_w2k_traceStats.bitbltMemoryReadBytes += readBytes;
	if (patterned) npdisp_w2k_traceStats.bitbltPattern++;
}

void npdisp_w2k_traceBitBltBatch(UINT64 calls, UINT64 bytes)
{
	npdisp_w2k_traceStats.bitbltBatchCalls += calls;
	npdisp_w2k_traceStats.bitbltBatchBytes += bytes;
}

void npdisp_w2k_traceOutput(UINT32 type, UINT64 pixels)
{
	if (type >= NPDISP_W2K_OUTPUT_TRACE_MAX) return;
	npdisp_w2k_traceStats.outputClass[type]++;
	npdisp_w2k_traceStats.outputClassPixels[type] += pixels;
}

void npdisp_w2k_traceFastRect(void)
{
	npdisp_w2k_traceStats.fastRectCalls++;
}

void npdisp_w2k_traceTextDirect(void)
{
	npdisp_w2k_traceStats.textDirectCalls++;
}

void npdisp_w2k_traceFastEllipse(void)
{
	npdisp_w2k_traceStats.fastEllipseCalls++;
}

#endif

