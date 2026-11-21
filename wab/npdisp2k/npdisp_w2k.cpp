/**
 * @file	npdisp_w2k.cpp
 * @brief	NPDISP Windows 2000 linear framebuffer implementation
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>

#include "cpucore.h"
#include "pccore.h"
#include "../wab.h"

#include "../npdispdef.h"
#include "../npdisp.h"
#include "../npdisp_internal.h"
#include "../npdisp_mem.h"
#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"
#include "../npdisp_dd.h"
#if defined(SUPPORT_NPDISP_D3D)
#include "../npdisp_d3d.h"
#endif
#include "../npdisp_palette.h"

#if defined(SUPPORT_IA32_HAXM)
#include "i386hax/haxfunc.h"
#include "i386hax/haxcore.h"
#endif
#if defined(SUPPORT_IA32_WHPX)
#include "i386whpx/whpxfunc.h"
#include "i386whpx/whpxcore.h"
#endif

#if !defined(CPUCORE_IA32)
#define cpu_kmemorywrite(a,v) memp_write8(a,v)
#define cpu_kmemorywrite_d(a,v) memp_write32(a,v)
#define cpu_kmemoryread(a) memp_read8(a)
#define cpu_kmemoryread_d(a) memp_read32(a)
#endif

#define NPDISP_W2K_MODE_STRIDE(width, bpp) ((((width) * (bpp) + 31) / 32) * 4)
#define NPDISP_W2K_MODE8(width, height)  { (width), (height),  8, NPDISP_W2K_MODE_STRIDE((width), 8),  0,          0,          0,          8, 8, 8 }
#define NPDISP_W2K_MODE16(width, height) { (width), (height), 16, NPDISP_W2K_MODE_STRIDE((width), 16), 0x0000f800, 0x000007e0, 0x0000001f, 5, 6, 5 }
#define NPDISP_W2K_MODE24(width, height) { (width), (height), 24, NPDISP_W2K_MODE_STRIDE((width), 24), 0x00ff0000, 0x0000ff00, 0x000000ff, 8, 8, 8 }
#define NPDISP_W2K_MODE32(width, height) { (width), (height), 32, NPDISP_W2K_MODE_STRIDE((width), 32), 0x00ff0000, 0x0000ff00, 0x000000ff, 8, 8, 8 }

extern NPDISP_WINDOWS npdispwin;

static const NPDISP_W2K_MODE_INFO npdisp_w2k_modes[] = {
	NPDISP_W2K_MODE8(320, 200),
	NPDISP_W2K_MODE16(320, 200),
	NPDISP_W2K_MODE24(320, 200),
	NPDISP_W2K_MODE32(320, 200),
	NPDISP_W2K_MODE8(320, 240),
	NPDISP_W2K_MODE16(320, 240),
	NPDISP_W2K_MODE24(320, 240),
	NPDISP_W2K_MODE32(320, 240),
	NPDISP_W2K_MODE8(640, 480),
	NPDISP_W2K_MODE16(640, 480),
	NPDISP_W2K_MODE24(640, 480),
	NPDISP_W2K_MODE32(640, 480),
	NPDISP_W2K_MODE8(768, 512),
	NPDISP_W2K_MODE16(768, 512),
	NPDISP_W2K_MODE24(768, 512),
	NPDISP_W2K_MODE32(768, 512),
	NPDISP_W2K_MODE8(800, 450),
	NPDISP_W2K_MODE16(800, 450),
	NPDISP_W2K_MODE24(800, 450),
	NPDISP_W2K_MODE32(800, 450),
	NPDISP_W2K_MODE8(800, 480),
	NPDISP_W2K_MODE16(800, 480),
	NPDISP_W2K_MODE24(800, 480),
	NPDISP_W2K_MODE32(800, 480),
	NPDISP_W2K_MODE8(800, 600),
	NPDISP_W2K_MODE16(800, 600),
	NPDISP_W2K_MODE24(800, 600),
	NPDISP_W2K_MODE32(800, 600),
	NPDISP_W2K_MODE8(1024, 576),
	NPDISP_W2K_MODE16(1024, 576),
	NPDISP_W2K_MODE24(1024, 576),
	NPDISP_W2K_MODE32(1024, 576),
	NPDISP_W2K_MODE8(1024, 600),
	NPDISP_W2K_MODE16(1024, 600),
	NPDISP_W2K_MODE24(1024, 600),
	NPDISP_W2K_MODE32(1024, 600),
	NPDISP_W2K_MODE8(1024, 768),
	NPDISP_W2K_MODE16(1024, 768),
	NPDISP_W2K_MODE24(1024, 768),
	NPDISP_W2K_MODE32(1024, 768),
	NPDISP_W2K_MODE8(1280, 720),
	NPDISP_W2K_MODE16(1280, 720),
	NPDISP_W2K_MODE24(1280, 720),
	NPDISP_W2K_MODE32(1280, 720),
	NPDISP_W2K_MODE8(1280, 800),
	NPDISP_W2K_MODE16(1280, 800),
	NPDISP_W2K_MODE24(1280, 800),
	NPDISP_W2K_MODE32(1280, 800),
	NPDISP_W2K_MODE8(1280, 960),
	NPDISP_W2K_MODE16(1280, 960),
	NPDISP_W2K_MODE24(1280, 960),
	NPDISP_W2K_MODE32(1280, 960),
	NPDISP_W2K_MODE8(1280, 1024),
	NPDISP_W2K_MODE16(1280, 1024),
	NPDISP_W2K_MODE24(1280, 1024),
	NPDISP_W2K_MODE32(1280, 1024),
	NPDISP_W2K_MODE8(1600, 900),
	NPDISP_W2K_MODE16(1600, 900),
	NPDISP_W2K_MODE24(1600, 900),
	NPDISP_W2K_MODE32(1600, 900),
	NPDISP_W2K_MODE8(1600, 1000),
	NPDISP_W2K_MODE16(1600, 1000),
	NPDISP_W2K_MODE24(1600, 1000),
	NPDISP_W2K_MODE32(1600, 1000),
	NPDISP_W2K_MODE8(1600, 1200),
	NPDISP_W2K_MODE16(1600, 1200),
	NPDISP_W2K_MODE24(1600, 1200),
	NPDISP_W2K_MODE32(1600, 1200),
	NPDISP_W2K_MODE8(1920, 1080),
	NPDISP_W2K_MODE16(1920, 1080),
	NPDISP_W2K_MODE24(1920, 1080),
	NPDISP_W2K_MODE32(1920, 1080),
	NPDISP_W2K_MODE8(1920, 1200),
	NPDISP_W2K_MODE16(1920, 1200),
	NPDISP_W2K_MODE24(1920, 1200),
	NPDISP_W2K_MODE32(1920, 1200),
	NPDISP_W2K_MODE8(1920, 1440),
	NPDISP_W2K_MODE16(1920, 1440),
	NPDISP_W2K_MODE24(1920, 1440),
	NPDISP_W2K_MODE32(1920, 1440),
	NPDISP_W2K_MODE8(2560, 1440),
	NPDISP_W2K_MODE16(2560, 1440),
	NPDISP_W2K_MODE24(2560, 1440),
	NPDISP_W2K_MODE32(2560, 1440),
	NPDISP_W2K_MODE8(2560, 1600),
	NPDISP_W2K_MODE16(2560, 1600),
	NPDISP_W2K_MODE24(2560, 1600),
	NPDISP_W2K_MODE32(2560, 1600),
	NPDISP_W2K_MODE8(3200, 1800),
	NPDISP_W2K_MODE16(3200, 1800),
	NPDISP_W2K_MODE24(3200, 1800),
	NPDISP_W2K_MODE32(3200, 1800),
	NPDISP_W2K_MODE8(3200, 2000),
	NPDISP_W2K_MODE16(3200, 2000),
	NPDISP_W2K_MODE24(3200, 2000),
	NPDISP_W2K_MODE32(3200, 2000),
	NPDISP_W2K_MODE8(3840, 2160),
	NPDISP_W2K_MODE16(3840, 2160),
	NPDISP_W2K_MODE24(3840, 2160),
	NPDISP_W2K_MODE32(3840, 2160),
	NPDISP_W2K_MODE8(3840, 2400),
	NPDISP_W2K_MODE16(3840, 2400),
	NPDISP_W2K_MODE24(3840, 2400),
	NPDISP_W2K_MODE32(3840, 2400)
};

#define NPDISP_W2K_MODE_COUNT ((UINT32)(sizeof(npdisp_w2k_modes) / sizeof(npdisp_w2k_modes[0])))

static UINT32 npdisp_w2k_pendingWidth = 640;
static UINT32 npdisp_w2k_pendingHeight = 480;
static UINT32 npdisp_w2k_pendingBpp = 8;
static UINT32 npdisp_w2k_pendingStride = 640;
static UINT32 npdisp_w2k_currentMode = 0;
static UINT32 npdisp_w2k_paletteIndex = 0;
static UINT32 npdisp_w2k_paletteRgb = 0;
static UINT32 npdisp_w2k_hostCallAddr = 0;
static UINT32 npdisp_w2k_hostCallStatus = NPDISP_W2K_HOSTCALL_IDLE;

#if defined(_WIN32)
static HANDLE npdisp_w2k_fbSection = NULL;
static UINT8 *npdisp_w2k_fbHost = NULL;
static bool npdisp_w2k_screenCreate = false;
#endif
#if defined(SUPPORT_IA32_HAXM)
static bool npdisp_w2k_haxRegistered = false;
static bool npdisp_w2k_haxMapped = false;
static std::vector<UINT8> npdisp_w2k_haxShadow;
#endif
#if defined(SUPPORT_IA32_WHPX)
static bool npdisp_w2k_whpxRegistered = false;
static bool npdisp_w2k_whpxMapped = false;
static std::vector<UINT8> npdisp_w2k_whpxShadow;
#endif

#if defined(_WIN32)
static int npdisp_w2k_prepareBacking(void)
{
	if (npdisp_w2k_fbSection && npdisp_w2k_fbHost) return 1;

	npdisp_w2k_fbSection = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, NPDISP_W2K_FB_SIZE, NULL);
	if (!npdisp_w2k_fbSection) return 0;
	npdisp_w2k_fbHost = (UINT8 *)MapViewOfFile(npdisp_w2k_fbSection, FILE_MAP_ALL_ACCESS, 0, 0, NPDISP_W2K_FB_SIZE);
	if (!npdisp_w2k_fbHost) {
		CloseHandle(npdisp_w2k_fbSection);
		npdisp_w2k_fbSection = NULL;
		return 0;
	}
	return 1;
}
#endif

int npdisp_w2k_beginScreenCreate(void)
{
#if defined(_WIN32)
	npdisp_w2k_screenCreate = false;
	if (!npdisp_w2k_prepareBacking()) return 0;
	npdisp_w2k_screenCreate = true;
#endif
	return 1;
}

void npdisp_w2k_endScreenCreate(void)
{
#if defined(_WIN32)
	npdisp_w2k_screenCreate = false;
#endif
}

#if defined(_WIN32)
HBITMAP npdisp_w2k_createPrimaryDIB(HDC hdc, BITMAPINFO *bi, void **bits)
{
	HANDLE section = NULL;
	if (npdisp_w2k_screenCreate) section = npdisp_w2k_fbSection;
	return CreateDIBSection(hdc, bi, DIB_RGB_COLORS, bits, section, 0);
}
#endif

void npdisp_w2k_haxInvalidate(void)
{
#if defined(SUPPORT_IA32_HAXM)
	npdisp_w2k_haxRegistered = false;
	npdisp_w2k_haxMapped = false;
#endif
#if defined(SUPPORT_IA32_WHPX)
	npdisp_w2k_whpxRegistered = false;
	npdisp_w2k_whpxMapped = false;
#endif
}

int npdisp_w2k_haxSync(void)
{
#if defined(SUPPORT_IA32_HAXM) && defined(_WIN32)
	HAX_ALLOC_RAM_INFO allocInfo = { 0 };
	HAX_SET_RAM_INFO ramInfo = { 0 };

	if (!np2hax.enable) return 1;
	if (!npdisp.w2kLfb) return 1;
	if (!np2hax.hVMDevice) return 1;
	if (!npdisp_w2k_fbHost) return 0;
	if ((((UINT64)npdisp_w2k_fbHost | (UINT64)NPDISP_W2K_FB_PHYS_BASE | (UINT64)NPDISP_W2K_FB_SIZE) & (UINT64)0xfff) != 0) {
		TRACEOUT(("NPDISP W2K HAX framebuffer alignment error: host=%p phys=%08x size=%08x",
			npdisp_w2k_fbHost, NPDISP_W2K_FB_PHYS_BASE, NPDISP_W2K_FB_SIZE));
		return 0;
	}

	if (!npdisp_w2k_haxRegistered) {
		allocInfo.size = NPDISP_W2K_FB_SIZE;
		allocInfo.va = (UINT64)npdisp_w2k_fbHost;
		if (i386haxfunc_allocRAM(&allocInfo) == FAILURE) {
			TRACEOUT(("NPDISP W2K HAX alloc framebuffer failed."));
			return 0;
		}
		npdisp_w2k_haxRegistered = true;
	}

	ramInfo.pa_start = NPDISP_W2K_FB_PHYS_BASE;
	ramInfo.size = NPDISP_W2K_FB_SIZE;
	ramInfo.flags = HAX_RAM_INFO_INVALID;
	ramInfo.va = 0;
	i386haxfunc_setRAM(&ramInfo);
	ramInfo.flags = 0;
	ramInfo.va = (UINT64)npdisp_w2k_fbHost;
	if (i386haxfunc_setRAM(&ramInfo) == FAILURE) {
		TRACEOUT(("NPDISP W2K HAX map framebuffer failed."));
		npdisp_w2k_haxMapped = false;
		return 0;
	}
	npdisp_w2k_haxMapped = true;

	if (npdisp.mm_screenPtr && npdisp.mm_screenSize) {
		memcpy(npdisp_w2k_fbHost, npdisp.mm_screenPtr, npdisp.mm_screenSize);
	}


	if (npdisp.mm_screenPtr && npdisp.mm_screenSize) {
		npdisp_w2k_haxShadow.resize(npdisp.mm_screenSize);
		memcpy(&npdisp_w2k_haxShadow[0], npdisp_w2k_fbHost, npdisp.mm_screenSize);
	}
	return 1;
#endif
#if defined(SUPPORT_IA32_WHPX) && defined(_WIN32)
	if (!np2whpx.enable) return 1;
	if (!npdisp.w2kLfb) return 1;
	if (!np2whpx.created) return 1;
	if (!npdisp_w2k_fbHost) return 0;
	if ((((UINT64)npdisp_w2k_fbHost | (UINT64)NPDISP_W2K_FB_PHYS_BASE | (UINT64)NPDISP_W2K_FB_SIZE) & (UINT64)0xfff) != 0) {
		TRACEOUT(("NPDISP W2K WHPX framebuffer alignment error: host=%p phys=%08x size=%08x",
			npdisp_w2k_fbHost, NPDISP_W2K_FB_PHYS_BASE, NPDISP_W2K_FB_SIZE));
		return 0;
	}

	i386whpxfunc_unmap_memory(NPDISP_W2K_FB_PHYS_BASE, NPDISP_W2K_FB_SIZE);
	if (i386whpxfunc_map_memory(NPDISP_W2K_FB_PHYS_BASE,
			npdisp_w2k_fbHost, NPDISP_W2K_FB_SIZE, 0) == FAILURE) {
		TRACEOUT(("NPDISP W2K WHPX map framebuffer failed."));
		npdisp_w2k_whpxMapped = false;
		return 0;
	}
	npdisp_w2k_whpxRegistered = true;
	npdisp_w2k_whpxMapped = true;

	if (npdisp.mm_screenPtr && npdisp.mm_screenSize) {
		memcpy(npdisp_w2k_fbHost, npdisp.mm_screenPtr, npdisp.mm_screenSize);
	}


	if (npdisp.mm_screenPtr && npdisp.mm_screenSize) {
		npdisp_w2k_whpxShadow.resize(npdisp.mm_screenSize);
		memcpy(&npdisp_w2k_whpxShadow[0], npdisp_w2k_fbHost, npdisp.mm_screenSize);
	}
	return 1;
#endif
#if !defined(SUPPORT_IA32_HAXM) && !defined(SUPPORT_IA32_WHPX)
	return 1;
#endif
}

void npdisp_w2k_haxUnmap(void)
{
#if defined(SUPPORT_IA32_HAXM) && defined(_WIN32)
	HAX_SET_RAM_INFO ramInfo = { 0 };

	if (npdisp_w2k_haxMapped && np2hax.enable && np2hax.hVMDevice) {
		ramInfo.pa_start = NPDISP_W2K_FB_PHYS_BASE;
		ramInfo.size = NPDISP_W2K_FB_SIZE;
		ramInfo.flags = HAX_RAM_INFO_INVALID;
		ramInfo.va = 0;
		i386haxfunc_setRAM(&ramInfo);
	}
	npdisp_w2k_haxMapped = false;
	npdisp_w2k_haxShadow.clear();
#endif
#if defined(SUPPORT_IA32_WHPX) && defined(_WIN32)
	if (npdisp_w2k_whpxMapped && np2whpx.enable && np2whpx.created) {
		i386whpxfunc_unmap_memory(NPDISP_W2K_FB_PHYS_BASE,
			NPDISP_W2K_FB_SIZE);
	}
	npdisp_w2k_whpxMapped = false;
	npdisp_w2k_whpxShadow.clear();
#endif
}

#if defined(SUPPORT_IA32_WHPX)
void npdisp_w2k_whpxInvalidate(void)
{
	npdisp_w2k_haxInvalidate();
}

int npdisp_w2k_whpxSync(void)
{
	return npdisp_w2k_haxSync();
}
#endif

int npdisp_w2k_haxIsMapped(void)
{
#if defined(SUPPORT_IA32_HAXM)
	return npdisp_w2k_haxMapped ? 1 : 0;
#endif
#if defined(SUPPORT_IA32_WHPX)
	return npdisp_w2k_whpxMapped ? 1 : 0;
#endif
#if !defined(SUPPORT_IA32_HAXM) && !defined(SUPPORT_IA32_WHPX)
	return 0;
#endif
}

void npdisp_w2k_haxPollDirty(void)
{
#if defined(SUPPORT_IA32_HAXM)
	UINT32 bytesPerPixel;
	int left = npdisp.width;
	int top = npdisp.height;
	int right = 0;
	int bottom = 0;

	if (!npdisp_w2k_haxMapped || !npdisp.w2kLfb || !npdisp.mm_screenPtr || !npdisp.mm_screenSize || !npdisp_w2k_fbHost) return;
	if (npdisp_w2k_haxShadow.size() != npdisp.mm_screenSize) {
		npdisp_w2k_haxShadow.resize(npdisp.mm_screenSize);
		memcpy(&npdisp_w2k_haxShadow[0], npdisp_w2k_fbHost, npdisp.mm_screenSize);
		return;
	}
	bytesPerPixel = (npdisp.bpp + 7) >> 3;
	if (!bytesPerPixel) return;

	for (int y = 0; y < npdisp.height; ++y) {
		UINT8 *src = npdisp_w2k_fbHost + (UINT32)y * npdispwin.stride;
		UINT8 *shadow = &npdisp_w2k_haxShadow[(UINT32)y * npdispwin.stride];
		if (memcmp(src, shadow, npdispwin.stride) != 0) {
			UINT32 first = 0;
			UINT32 last = npdispwin.stride;
			while (first < (UINT32)npdispwin.stride && src[first] == shadow[first]) ++first;
			while (last > first && src[last - 1] == shadow[last - 1]) --last;
			if (first < last) {
				memcpy(shadow + first, src + first, last - first);
				int rowLeft = (int)(first / bytesPerPixel);
				int rowRight = (int)((last + bytesPerPixel - 1) / bytesPerPixel);
				if (rowLeft < left) left = rowLeft;
				if (rowRight > right) right = rowRight;
				if (y < top) top = y;
				if (y + 1 > bottom) bottom = y + 1;
			}
		}
	}
	if (left < right && top < bottom) {
		npdisp_setDirty(left, top, right, bottom);
		npdisp.updated = 1;
	}
#endif
#if defined(SUPPORT_IA32_WHPX)
	UINT32 bytesPerPixel;
	int left = npdisp.width;
	int top = npdisp.height;
	int right = 0;
	int bottom = 0;

	if (!npdisp_w2k_whpxMapped || !npdisp.w2kLfb || !npdisp.mm_screenPtr || !npdisp.mm_screenSize || !npdisp_w2k_fbHost) return;
	if (npdisp_w2k_whpxShadow.size() != npdisp.mm_screenSize) {
		npdisp_w2k_whpxShadow.resize(npdisp.mm_screenSize);
		memcpy(&npdisp_w2k_whpxShadow[0], npdisp_w2k_fbHost, npdisp.mm_screenSize);
		return;
	}
	bytesPerPixel = (npdisp.bpp + 7) >> 3;
	if (!bytesPerPixel) return;

	for (int y = 0; y < npdisp.height; ++y) {
		UINT8 *src = npdisp_w2k_fbHost + (UINT32)y * npdispwin.stride;
		UINT8 *shadow = &npdisp_w2k_whpxShadow[(UINT32)y * npdispwin.stride];
		if (memcmp(src, shadow, npdispwin.stride) != 0) {
			UINT32 first = 0;
			UINT32 last = npdispwin.stride;
			while (first < (UINT32)npdispwin.stride && src[first] == shadow[first]) ++first;
			while (last > first && src[last - 1] == shadow[last - 1]) --last;
			if (first < last) {
				memcpy(shadow + first, src + first, last - first);
				int rowLeft = (int)(first / bytesPerPixel);
				int rowRight = (int)((last + bytesPerPixel - 1) / bytesPerPixel);
				if (rowLeft < left) left = rowLeft;
				if (rowRight > right) right = rowRight;
				if (y < top) top = y;
				if (y + 1 > bottom) bottom = y + 1;
			}
		}
	}
	if (left < right && top < bottom) {
		npdisp_setDirty(left, top, right, bottom);
		npdisp.updated = 1;
	}
#endif
}

UINT8 *npdisp_w2k_getFrameBufferHost(void)
{
#if defined(_WIN32)
	return npdisp_w2k_fbHost;
#else
	return NULL;
#endif
}

int npdisp_w2k_isFrameBufferAddress(const void *ptr)
{
#if defined(_WIN32)
	const UINT8 *p = (const UINT8 *)ptr;
	return p && npdisp_w2k_fbHost && p >= npdisp_w2k_fbHost && p < npdisp_w2k_fbHost + NPDISP_W2K_FB_SIZE;
#else
	(void)ptr;
	return 0;
#endif
}

void npdisp_w2k_shutdown(void)
{
	npdisp_w2k_haxUnmap();
#if defined(SUPPORT_IA32_HAXM)
	npdisp_w2k_haxRegistered = false;
	npdisp_w2k_haxShadow.clear();
#endif
#if defined(SUPPORT_IA32_WHPX)
	npdisp_w2k_whpxRegistered = false;
	npdisp_w2k_whpxShadow.clear();
#endif
#if defined(_WIN32)
	if (npdisp.mm_ddOffscreenPtr && npdisp_w2k_isFrameBufferAddress(npdisp.mm_ddOffscreenPtr)) {
		npdisp.mm_ddOffscreenPtr = NULL;
		npdisp.mm_ddOffscreenSize = 0;
	}
	if (npdisp_w2k_fbHost) {
		UnmapViewOfFile(npdisp_w2k_fbHost);
		npdisp_w2k_fbHost = NULL;
	}
	if (npdisp_w2k_fbSection) {
		CloseHandle(npdisp_w2k_fbSection);
		npdisp_w2k_fbSection = NULL;
	}
	npdisp_w2k_screenCreate = false;
#endif
}

int npdisp_w2k_guestRead(UINT32 vaddr, void *buffer, UINT32 size)
{
#if defined(CPUCORE_IA32)
	if (!npdisp_w2k_readLinearRam(vaddr, buffer, size)) return 0;
#else
	return 0;
#endif
	npdisp_w2k_traceGuestRead(size, size / 4);
	CPU_REMCLOCK -= (SINT32)(size / 4);
	return 1;
}

int npdisp_w2k_guestReadStrided(UINT32 vaddr, SINT32 srcStride, void *buffer, UINT32 dstStride, UINT32 rowBytes, UINT32 rows)
{
#if defined(CPUCORE_IA32)
	UINT64 totalSize = (UINT64)rowBytes * rows;
	UINT8 *dst = (UINT8 *)buffer;
	if (!buffer || !rowBytes || !rows || totalSize > 0xffffffffULL || dstStride < rowBytes) return 0;
	if (srcStride > 0 && (UINT32)srcStride == rowBytes && dstStride == rowBytes) {
		if (!npdisp_w2k_readLinearRam(vaddr, dst, (UINT32)totalSize)) return 0;
	}
	else if (!npdisp_w2k_readLinearRows(vaddr, srcStride, dst, dstStride, rowBytes, rows)) return 0;
	npdisp_w2k_traceGuestRead((UINT32)totalSize, (UINT32)(totalSize / 64));
	CPU_REMCLOCK -= (SINT32)(totalSize / 64);
	return 1;
#else
	return 0;
#endif
}

int npdisp_w2k_guestWrite(UINT32 vaddr, const void *buffer, UINT32 size)
{
#if defined(CPUCORE_IA32)
	if (!npdisp_w2k_writeLinearRam(vaddr, buffer, size)) return 0;
#else
	return 0;
#endif
	npdisp_w2k_traceGuestWrite(size, size / 4);
	CPU_REMCLOCK -= (SINT32)(size / 4);
	return 1;
}

void npdisp_w2k_chargePixels(UINT64 pixels)
{
	UINT64 clocks;

	if (pixels == 0) return;
	clocks = pixels * (UINT64)pccore.multiple / 8000;
	if (clocks > 0x3fffffffULL) clocks = 0x3fffffffULL;
	npdisp_w2k_traceChargePixels(pixels, (UINT32)clocks);
	CPU_REMCLOCK -= (SINT32)clocks;
}

static int npdisp_w2k_syncGuestCpu(void)
{
#if defined(SUPPORT_IA32_HAXM)
	if (np2hax.enable && np2haxcore.running && !np2hax.emumode) {
		if (!np2hax.hVMDevice || i386haxfunc_vcpu_getREGs(&np2haxstat.state) != SUCCESS) return 0;
		np2haxstat.update_regs = 0;
		ia32hax_copyregHAXtoNP2();
		return 1;
	}
#endif
#if defined(SUPPORT_IA32_WHPX)
	if (np2whpx.enable && np2whpxcore.running) {
		if (!np2whpx.created || i386whpxfunc_get_registers(&np2whpxstat.state) != SUCCESS) return 0;
		np2whpxstat.update_regs = 0;
		ia32whpx_copyregWHPXtoNP2();
		return 1;
	}
#endif
	// The software core already owns the current CPU state.
	return 1;
}

static int npdisp_w2k_guestWritable(UINT32 addr, UINT32 size)
{
#if defined(CPUCORE_IA32)
	return npdisp_w2k_checkLinearRows(addr, 0, size, 1, 1);
#else
	return 0;
#endif
}

static UINT32 npdisp_w2k_stride(UINT32 width, UINT32 bpp)
{
	return ((width * bpp + 31) / 32) * 4;
}

static int npdisp_w2k_validMode(void)
{
	UINT64 size;

	if (npdisp_w2k_pendingWidth < 160 || npdisp_w2k_pendingWidth > WAB_MAX_WIDTH) return 0;
	if (npdisp_w2k_pendingHeight < 100 || npdisp_w2k_pendingHeight > WAB_MAX_HEIGHT) return 0;
	if (npdisp_w2k_pendingBpp != 8 && npdisp_w2k_pendingBpp != 16 && npdisp_w2k_pendingBpp != 24 && npdisp_w2k_pendingBpp != 32) return 0;
	if (npdisp_w2k_pendingStride != npdisp_w2k_stride(npdisp_w2k_pendingWidth, npdisp_w2k_pendingBpp)) return 0;
	size = (UINT64)npdisp_w2k_pendingStride * npdisp_w2k_pendingHeight;
	return size != 0 && size <= NPDISP_W2K_FB_PRIMARY_SIZE;
}

static void npdisp_w2k_resetMode(void)
{
	npdisp_w2k_haxUnmap();
	if (!npdisp.w2kLfb) return;
	npdisp.enabled = 0;
	npdisp.active = 0;
	npdisp.w2kLfb = 0;
	npdisp.mm_vramPhysicalAddr = 0;
	np2wab.relaystateext = 0;
	np2wab_setRelayState(np2wab.relaystateint | np2wab.relaystateext);
}

static int npdisp_w2k_commitMode(void)
{
	bool resize;

	if (!npdisp.ioenabled || !npdisp_w2k_validMode()) return 0;

	resize = npdisp.enabled && npdisp.active;
	npdisp.width = npdisp_w2k_pendingWidth;
	npdisp.height = npdisp_w2k_pendingHeight;
	npdisp.bpp = npdisp_w2k_pendingBpp;
	npdisp.dpiX = 96;
	npdisp.dpiY = 96;
	npdisp.usePalette = (npdisp.bpp == 8);
	npdisp.isWin9x = 0;
	npdisp.w2kLfb = 0;
	npdisp.mm_vramPhysicalAddr = 0;
	npdisp.mm_vramLinearAddr = 0;
	npdisp.mm_vramSelector = 0;
	npdisp.mm_dciEnable = 0;
	npdisp.mm_ddCallbacksAddr = 0;
	npdisp.mm_ddSurfaceCallbacksAddr = 0;
	npdisp.mm_ddPaletteCallbacksAddr = 0;
	npdisp.mm_ddHalInfoAddr = 0;
	npdisp.mm_ddModeInfoAddr = 0;
	npdisp.mm_ddVidMemAddr = 0;
	npdisp.mm_ddBridgeInfoAddr = 0;
	npdisp.mm_d3dGlobalDriverDataAddr = 0;
	npdisp.mm_d3dHalCallbacksAddr = 0;

	if (!npdisp_w2k_beginScreenCreate()) {
		npdisp.enabled = 0;
		npdisp.active = 0;
		return 0;
	}
	npdisp_createScreen(resize);
	npdisp_w2k_endScreenCreate();
	if (!npdispwin.pBits || !npdisp.mm_screenPtr || npdisp.mm_screenSize > NPDISP_W2K_FB_PRIMARY_SIZE) {
		npdisp_w2k_haxUnmap();
		npdisp.enabled = 0;
		npdisp.active = 0;
		return 0;
	}

	npdisp.w2kLfb = 1;
	npdisp.mm_vramPhysicalAddr = NPDISP_W2K_FB_PHYS_BASE;
	if (npdisp.acceleration >= NPDISP_ACCEL_DIRECTDRAW && !npdisp_dd_ensureOffscreenBacking()) {
		TRACEOUT(("NPDISP W2K DirectDraw offscreen allocation failed."));
	}
	if (!npdisp_w2k_haxSync()) {
		npdisp.w2kLfb = 0;
		npdisp.mm_vramPhysicalAddr = 0;
		npdisp.enabled = 0;
		npdisp.active = 0;
		return 0;
	}
	npdisp.enabled = 1;
	npdisp.active = 1;
	np2wab.realWidth = 0;
	np2wab.realHeight = 0;
	np2wab.relaystateext = 3;
	np2wab_setRelayState(np2wab.relaystateint | np2wab.relaystateext);
	npdisp_setDirtyAll();
	npdisp.updated = 1;
	return 1;
}

static int npdisp_w2k_setMode(UINT32 index)
{
	const NPDISP_W2K_MODE_INFO *mode;

	if (index >= NPDISP_W2K_MODE_COUNT) return 0;
	mode = &npdisp_w2k_modes[index];
	npdisp_w2k_pendingWidth = mode->width;
	npdisp_w2k_pendingHeight = mode->height;
	npdisp_w2k_pendingBpp = mode->bitsPerPixel;
	npdisp_w2k_pendingStride = mode->stride;
	if (!npdisp_w2k_commitMode()) return 0;
	npdisp_w2k_currentMode = index;
	return 1;
}

static UINT32 npdisp_w2k_hostCallTest(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_TEST_REQUEST request;
	NPDISP_W2K_HOSTCALL_TEST_TARGET target;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size != sizeof(request) || request.targetAddr == 0) {
		return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	}
	if (!npdisp_w2k_guestRead(request.targetAddr, &target, sizeof(target))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (!npdisp_w2k_guestWritable(requestAddr, sizeof(request)) ||
		!npdisp_w2k_guestWritable(request.targetAddr, sizeof(target))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	request.result = request.value ^ NPDISP_W2K_HOSTCALL_TEST_XOR;
	target.result = target.value ^ NPDISP_W2K_HOSTCALL_TEST_XOR;
	if (!npdisp_w2k_guestWrite(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (!npdisp_w2k_guestWrite(request.targetAddr, &target, sizeof(target))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	return NPDISP_W2K_HOSTCALL_OK;
}

static UINT32 npdisp_w2k_hostCallGetInfo(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_INFO_REQUEST request;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size != sizeof(request)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	request.modeCount = NPDISP_W2K_MODE_COUNT;
	request.currentMode = npdisp_w2k_currentMode;
	if (!npdisp_w2k_guestWrite(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	return NPDISP_W2K_HOSTCALL_OK;
}

static UINT32 npdisp_w2k_hostCallGetMode(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_MODE_REQUEST request;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size != sizeof(request) || request.modeIndex >= NPDISP_W2K_MODE_COUNT) {
		return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	}
	request.mode = npdisp_w2k_modes[request.modeIndex];
	if (!npdisp_w2k_guestWrite(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	return NPDISP_W2K_HOSTCALL_OK;
}

static UINT32 npdisp_w2k_hostCallSetMode(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_SET_MODE_REQUEST request;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size != sizeof(request) || request.modeIndex >= NPDISP_W2K_MODE_COUNT) {
		return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	}
	return npdisp_w2k_setMode(request.modeIndex) ? NPDISP_W2K_HOSTCALL_OK : NPDISP_W2K_HOSTCALL_BAD_REQUEST;
}

static UINT32 npdisp_w2k_hostCallPointer(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_POINTER_REQUEST request;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size < sizeof(request) || request.command != NPDISP_W2K_HOSTCALL_POINTER) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.operation == NPDISP_W2K_POINTER_SET_ALPHA) {
		NPDISP_W2K_CURSOR_SHAPE shape;
		if (request.dataOffset < sizeof(request) || request.dataOffset > request.size ||
			request.dataSize != request.size - request.dataOffset || request.dataSize < sizeof(shape) ||
			(UINT64)requestAddr + request.size > 0x100000000ULL) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		UINT32 shapeAddr = requestAddr + request.dataOffset;
		if (!npdisp_w2k_guestRead(shapeAddr, &shape, sizeof(shape)) ||
			!shape.width || !shape.height || shape.width > NPDISP_W2K_POINTER_MAX_WIDTH || shape.height > NPDISP_W2K_POINTER_MAX_HEIGHT ||
			shape.hotX >= shape.width || shape.hotY >= shape.height || shape.color != NPDISP_W2K_POINTER_ALPHA_FORMAT ||
			shape.widthBytes != shape.width * 4U || request.dataSize != sizeof(shape) + (UINT32)shape.widthBytes * shape.height ||
			(request.x >= 0 && (request.x >= 0xffff || request.y < 0 || request.y >= 0xffff))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		std::vector<UINT8> pixels((UINT32)shape.widthBytes * shape.height);
		if (!npdisp_w2k_guestRead(shapeAddr + sizeof(shape), &pixels[0], (UINT32)pixels.size()) ||
			!npdisp_setAlphaCursor(shape.width, shape.height, shape.hotX, shape.hotY,
				request.x < 0 ? 0xffff : (UINT16)request.x, request.x < 0 ? 0xffff : (UINT16)request.y, &pixels[0])) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		return NPDISP_W2K_HOSTCALL_OK;
	}
	if (request.operation == NPDISP_W2K_POINTER_SET) {
		NPDISP_W2K_CURSOR_SHAPE shape;
		UINT32 maskBpp;
		UINT32 xorBpp;
		UINT32 maskStride;
		UINT32 xorStride;
		UINT64 expected;
		UINT32 shapeAddr;

		if (request.dataOffset < sizeof(request) || request.dataOffset > request.size || request.dataSize > request.size - request.dataOffset || request.dataSize < sizeof(shape)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if ((UINT64)requestAddr + request.dataOffset + request.dataSize > 0x100000000ULL) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		shapeAddr = requestAddr + request.dataOffset;
		if (!npdisp_w2k_guestRead(shapeAddr, &shape, sizeof(shape))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if (!shape.width || !shape.height || shape.width > NPDISP_W2K_POINTER_MAX_WIDTH || shape.height > NPDISP_W2K_POINTER_MAX_HEIGHT || (shape.width & 15) != 0 || shape.hotX >= shape.width || shape.hotY >= shape.height) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		maskBpp = shape.color & 0xff;
		xorBpp = (shape.color >> 8) & 0xff;
		if (maskBpp != 1 || (xorBpp != 1 && xorBpp != 8 && xorBpp != 16 && xorBpp != 24 && xorBpp != 32)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		maskStride = ((shape.width + 15) >> 4) << 1;
		xorStride = xorBpp == 1 ? maskStride : ((shape.width * xorBpp + 31) >> 5) << 2;
		expected = sizeof(shape) + (UINT64)maskStride * shape.height + (UINT64)xorStride * shape.height;
		if (expected != request.dataSize || expected > 0xffffffffULL) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if (request.x >= 0 && (request.x > 0xffff || request.y < 0 || request.y > 0xffff)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
#if defined(CPUCORE_IA32)
		if (!npdisp.w2kLfb || !npdisp_w2k_checkLinearRows(shapeAddr, 0, request.dataSize, 1, 0)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
#else
		return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
#endif
		npdisp_setCursorLinear(shapeAddr);
		if (request.x < 0) npdisp_moveCursorAbsolute(0xffff, 0xffff);
		else npdisp_moveCursorAbsolute((UINT16)request.x, (UINT16)request.y);
		return NPDISP_W2K_HOSTCALL_OK;
	}
	if (request.operation == NPDISP_W2K_POINTER_MOVE) {
		if (request.size != sizeof(request) || request.x < 0 || request.x > 0xffff || request.y < 0 || request.y > 0xffff) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_moveCursorAbsolute((UINT16)request.x, (UINT16)request.y);
		return NPDISP_W2K_HOSTCALL_OK;
	}
	if (request.operation == NPDISP_W2K_POINTER_HIDE) {
		if (request.size != sizeof(request)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_moveCursorAbsolute(0xffff, 0xffff);
		return NPDISP_W2K_HOSTCALL_OK;
	}
	if (request.operation == NPDISP_W2K_POINTER_CLEAR) {
		if (request.size != sizeof(request)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_setCursorLinear(0);
		return NPDISP_W2K_HOSTCALL_OK;
	}
	return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
}

static UINT32 npdisp_w2k_getDirectDrawSetupInfo(void)
{
#if defined(SUPPORT_IA32_HAXM)
	if (np2hax.enable && npdisp.acceleration < NPDISP_ACCEL_DIRECT3D) return NPDISP_W2K_DDRAW_SETUPINFO_HEL | NPDISP_W2K_DDRAW_SETUPINFO_OVERLAY;
#endif
#if defined(SUPPORT_IA32_WHPX)
	if (np2whpx.enable && npdisp.acceleration < NPDISP_ACCEL_DIRECT3D) return NPDISP_W2K_DDRAW_SETUPINFO_HEL | NPDISP_W2K_DDRAW_SETUPINFO_OVERLAY;
#endif
	return NPDISP_W2K_DDRAW_SETUPINFO_HOST_ACCEL | NPDISP_W2K_DDRAW_SETUPINFO_OVERLAY;
}

static void npdisp_w2k_getDirectDrawCaps(NPDISP_W2K_DDRAW_CAPS* caps, UINT32 setupInfo)
{
	const bool helMode = (setupInfo & NPDISP_W2K_DDRAW_SETUPINFO_MODE_MASK) == NPDISP_W2K_DDRAW_SETUPINFO_HEL;

	memset(caps, 0, sizeof(*caps));
	caps->version = NPDISP_W2K_DDRAW_CAPS_VERSION;
	caps->size = sizeof(*caps);
	caps->coreCaps.dwSize = sizeof(caps->coreCaps);
	caps->coreCaps.dwVidMemTotal = NPDISP_W2K_FB_OFFSCREEN_SIZE;
	caps->coreCaps.dwVidMemFree = NPDISP_W2K_FB_OFFSCREEN_SIZE;
	caps->coreCaps.dwMaxVisibleOverlays = 1;
	caps->coreCaps.dwMinOverlayStretch = 1;
	caps->coreCaps.dwMaxOverlayStretch = 32000;
	caps->coreCaps.ddsCaps.dwCaps = NPDISP_DDSCAPS_PRIMARYSURFACE | NPDISP_DDSCAPS_OVERLAY | NPDISP_DDSCAPS_VIDEOMEMORY;
	caps->coreCaps.dwCaps = NPDISP_DDCAPS_OVERLAY | NPDISP_DDCAPS_OVERLAYCANTCLIP | NPDISP_DDCAPS_OVERLAYSTRETCH |
		NPDISP_DDCAPS_COLORKEY | NPDISP_DDCAPS_BLT | NPDISP_DDCAPS_BLTCOLORFILL |
		NPDISP_DDCAPS_BLTSTRETCH | NPDISP_DDCAPS_READSCANLINE;
	caps->coreCaps.dwCKeyCaps = NPDISP_DDCKEYCAPS_DESTOVERLAY | NPDISP_DDCKEYCAPS_DESTOVERLAYCLRSPACE |
		NPDISP_DDCKEYCAPS_SRCOVERLAY | NPDISP_DDCKEYCAPS_SRCOVERLAYCLRSPACE;
	caps->coreCaps.dwFXCaps = NPDISP_DDFXCAPS_OVERLAYSHRINKX | NPDISP_DDFXCAPS_OVERLAYSHRINKXN |
		NPDISP_DDFXCAPS_OVERLAYSHRINKY | NPDISP_DDFXCAPS_OVERLAYSHRINKYN | NPDISP_DDFXCAPS_OVERLAYSTRETCHX |
		NPDISP_DDFXCAPS_OVERLAYSTRETCHXN | NPDISP_DDFXCAPS_OVERLAYSTRETCHY | NPDISP_DDFXCAPS_OVERLAYSTRETCHYN |
		NPDISP_DDFXCAPS_OVERLAYMIRRORLEFTRIGHT | NPDISP_DDFXCAPS_OVERLAYMIRRORUPDOWN;
#if defined(SUPPORT_NPDISP_D3D)
	if (npdisp.acceleration >= NPDISP_ACCEL_DIRECT3D && npdisp.bpp >= 15) {
		caps->coreCaps.dwCaps |= NPDISP_DDCAPS_3D;
		caps->coreCaps.dwZBufferBitDepths = NPDISP_DDBD_16;
		caps->coreCaps.ddsCaps.dwCaps |= NPDISP_DDSCAPS_3DDEVICE | NPDISP_DDSCAPS_TEXTURE |
			NPDISP_DDSCAPS_MIPMAP | NPDISP_DDSCAPS_ZBUFFER;
	}
#endif

	if (helMode) return;

	caps->coreCaps.dwCaps2 |= NPDISP_DDCAPS2_WIDESURFACES;

#if defined(SUPPORT_NPDISP_D3D)
	if (npdisp.acceleration >= NPDISP_ACCEL_DIRECT3D && npdisp.bpp >= 15) {
		caps->coreCaps.dwCaps2 |= NPDISP_DDCAPS2_CANRENDERWINDOWED;
		/* Probe: route unscaled SRCCOPY system->video texture uploads to the HAL.
		 * All other system-memory blits may still fall back to the runtime. */
		caps->coreCaps.dwCaps |= NPDISP_DDCAPS_CANBLTSYSMEM;
		caps->coreCaps.dwSVBCaps |= NPDISP_DDCAPS_BLT;
		caps->coreCaps.dwSVBRops[0xccU >> 5] |= 1UL << (0xccU & 31);
	}
#endif

	caps->coreCaps.dwCKeyCaps |= NPDISP_DDCKEYCAPS_DESTBLT | NPDISP_DDCKEYCAPS_DESTBLTCLRSPACE |
		NPDISP_DDCKEYCAPS_SRCBLT | NPDISP_DDCKEYCAPS_SRCBLTCLRSPACE;
	caps->coreCaps.dwFXCaps |= NPDISP_DDFXCAPS_BLTMIRRORLEFTRIGHT | NPDISP_DDFXCAPS_BLTMIRRORUPDOWN |
		NPDISP_DDFXCAPS_BLTSHRINKX | NPDISP_DDFXCAPS_BLTSHRINKXN |
		NPDISP_DDFXCAPS_BLTSHRINKY | NPDISP_DDFXCAPS_BLTSHRINKYN |
		NPDISP_DDFXCAPS_BLTSTRETCHX | NPDISP_DDFXCAPS_BLTSTRETCHXN |
		NPDISP_DDFXCAPS_BLTSTRETCHY | NPDISP_DDFXCAPS_BLTSTRETCHYN;
	for (UINT32 rop = 0; rop < 256; ++rop) {
		caps->coreCaps.dwRops[rop >> 5] |= 1UL << (rop & 31);
	}
	caps->coreCaps.ddsCaps.dwCaps |= NPDISP_DDSCAPS_OFFSCREENPLAIN | NPDISP_DDSCAPS_VISIBLE |
		NPDISP_DDSCAPS_FLIP | NPDISP_DDSCAPS_FRONTBUFFER | NPDISP_DDSCAPS_BACKBUFFER | NPDISP_DDSCAPS_COMPLEX;
}

static UINT32 npdisp_w2k_hostCallDirectDraw(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_DDRAW_REQUEST request;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size != sizeof(request)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (!npdisp.mm_screenPtr || !npdisp.mm_ddOffscreenPtr || npdisp.mm_ddOffscreenSize != NPDISP_W2K_FB_OFFSCREEN_SIZE) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;

	if (!npdisp_w2k_guestWritable(requestAddr, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.operation == NPDISP_W2K_DDRAW_SETUP) {
		UINT32 setupInfo;

		if (!request.frameBufferAddr || (request.dataAddr && !npdisp_w2k_guestWritable(request.dataAddr, sizeof(NPDISP_W2K_DDRAW_CAPS)))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp.mm_vramLinearAddr = request.frameBufferAddr;
		npdisp.mm_ddScanoutOffset = 0;
		npdisp.mm_ddLastScanoutOffset = 0;
		npdisp.mm_ddPendingFlipOffset = 0;
		npdisp.mm_ddFlipPending = 0;
		setupInfo = npdisp_w2k_getDirectDrawSetupInfo();
		if (request.dataAddr) {
			NPDISP_W2K_DDRAW_CAPS caps;
			npdisp_w2k_getDirectDrawCaps(&caps, setupInfo);
			if (!npdisp_w2k_guestWrite(request.dataAddr, &caps, sizeof(caps))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		}
		request.callbackId = setupInfo;
		request.result = 1;
	}
	else if (request.operation == NPDISP_W2K_DDRAW_CALLBACK) {
		if (!request.dataAddr || !request.callbackId) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		request.result = npdisp_dd_dispatchBridge(request.callbackId, request.dataAddr);
	}
	else return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (!npdisp_w2k_guestWrite(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	return NPDISP_W2K_HOSTCALL_OK;
}

#if defined(SUPPORT_NPDISP_D3D)
static UINT32 npdisp_w2k_hostCallDirect3D(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_D3D_REQUEST request;
	NPDISP_W2K_D3D_CAPS caps;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.Version != NPDISP_W2K_HOSTCALL_VERSION || request.Size != sizeof(request)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (npdisp.acceleration < NPDISP_ACCEL_DIRECT3D || npdisp.bpp < 15) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;

	if (!npdisp_w2k_guestWritable(requestAddr, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.Operation == NPDISP_W2K_D3D_SETUP) {
		if (!request.DataAddr || request.DataSize < sizeof(caps) || !npdisp_d3d_getW2kCaps(&caps)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		// rev107 did not request a caps revision (CallbackId == 0).
		if (request.CallbackId == 0) caps.Version = 0x00010001UL;
		else if (request.CallbackId != NPDISP_W2K_D3D_CAPS_VERSION) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if (!npdisp_w2k_guestWrite(request.DataAddr, &caps, sizeof(caps))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		request.Result = 1;
	}
	else if (request.Operation == NPDISP_W2K_D3D_CALLBACK) {
		if (!request.DataAddr || !request.CallbackId || !npdisp_d3d_getW2kCaps(&caps)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if ((caps.SetupInfo & NPDISP_W2K_D3D_SETUPINFO_CALLBACKS_ACTIVE) == 0) request.Result = NPDISP_DDHAL_DRIVER_NOTHANDLED;
		else request.Result = npdisp_d3d_dispatchW2k(request.CallbackId, request.DataAddr, request.DataSize);
	}
	else return NPDISP_W2K_HOSTCALL_UNSUPPORTED;

	if (!npdisp_w2k_guestWrite(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	return NPDISP_W2K_HOSTCALL_OK;
}
#endif

static void npdisp_w2k_hostCall(UINT32 command)
{
	UINT32 status;

	if (npdisp_w2k_hostCallAddr == 0) {
		status = NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_w2k_hostCallStatus = status;
		npdisp_w2k_traceHostCall(command, status);
		return;
	}
	if (!npdisp_w2k_syncGuestCpu()) {
		npdisp_w2k_hostCallStatus = NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_w2k_traceHostCall(command, npdisp_w2k_hostCallStatus);
		return;
	}
	switch (command) {
	case NPDISP_W2K_HOSTCALL_TEST:
		status = npdisp_w2k_hostCallTest(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_GET_INFO:
		status = npdisp_w2k_hostCallGetInfo(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_GET_MODE:
		status = npdisp_w2k_hostCallGetMode(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_SET_MODE:
		status = npdisp_w2k_hostCallSetMode(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_BITBLT:
		status = npdisp_w2k_hostCallBitBlt(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_OUTPUT:
		status = npdisp_w2k_hostCallOutput(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_GDI:
		status = npdisp_w2k_hostCallGdi(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_DDRAW:
		status = npdisp_w2k_hostCallDirectDraw(npdisp_w2k_hostCallAddr);
		break;
	case NPDISP_W2K_HOSTCALL_POINTER:
		status = npdisp_w2k_hostCallPointer(npdisp_w2k_hostCallAddr);
		break;
#if defined(SUPPORT_NPDISP_D3D)
	case NPDISP_W2K_HOSTCALL_D3D:
		status = npdisp_w2k_hostCallDirect3D(npdisp_w2k_hostCallAddr);
		break;
#endif
	default:
		status = NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		break;
	}
	npdisp_w2k_hostCallStatus = status;
	npdisp_w2k_traceHostCall(command, status);
}

// Read-only status register. Rendering progresses while the guest polls.
static UINT32 npdisp_w2k_readSyncStatus(void)
{
#if defined(SUPPORT_NPDISP_D3D)
	if (!npdisp_d3d_trySynchronize()) return NPDISP_W2K_SYNC_BUSY;
#endif
	return NPDISP_W2K_SYNC_READY;
}

static UINT32 npdisp_w2k_readRegister(UINT32 offset)
{
	switch (offset) {
	case NPDISP_W2K_REG_SIGNATURE:
		return NPDISP_W2K_SIGNATURE;
	case NPDISP_W2K_REG_VERSION:
		return NPDISP_W2K_VERSION;
	case NPDISP_W2K_REG_WIDTH:
		return npdisp.w2kLfb ? npdisp.width : npdisp_w2k_pendingWidth;
	case NPDISP_W2K_REG_HEIGHT:
		return npdisp.w2kLfb ? npdisp.height : npdisp_w2k_pendingHeight;
	case NPDISP_W2K_REG_BPP:
		return npdisp.w2kLfb ? npdisp.bpp : npdisp_w2k_pendingBpp;
	case NPDISP_W2K_REG_STRIDE:
		return npdisp.w2kLfb ? (UINT32)npdispwin.stride : npdisp_w2k_pendingStride;
	case NPDISP_W2K_REG_FLAGS:
	{
		UINT32 flags = NPDISP_W2K_FLAG_PRESENT | NPDISP_W2K_FLAG_HOSTCALL | NPDISP_W2K_FLAG_HOST_MODEINFO |
			NPDISP_W2K_FLAG_HOST_BITBLT | NPDISP_W2K_FLAG_HOST_OUTPUT | NPDISP_W2K_FLAG_HOST_TEXT |
			NPDISP_W2K_FLAG_HOST_STRETCH | NPDISP_W2K_FLAG_HOST_PATTERN | NPDISP_W2K_FLAG_HOST_POINTER | NPDISP_W2K_FLAG_HOST_ALPHA_POINTER;
		if (npdisp.acceleration >= NPDISP_ACCEL_DIRECTDRAW) flags |= NPDISP_W2K_FLAG_HOST_DDRAW;
#if defined(SUPPORT_NPDISP_D3D)
		if (npdisp.acceleration >= NPDISP_ACCEL_DIRECT3D) {
			flags |= NPDISP_W2K_FLAG_HOST_SYNC_POLL;
			if (npdisp.bpp >= 15) flags |= NPDISP_W2K_FLAG_HOST_D3D;
		}
#endif
		if (npdisp.w2kLfb && npdisp.enabled) flags |= NPDISP_W2K_FLAG_ENABLED;
		return flags;
	}
	case NPDISP_W2K_REG_FB_BASE:
		return NPDISP_W2K_FB_PHYS_BASE;
	case NPDISP_W2K_REG_FB_SIZE:
		return NPDISP_W2K_FB_SIZE;
	case NPDISP_W2K_REG_HOSTCALL_ADDR:
		return npdisp_w2k_hostCallAddr;
	case NPDISP_W2K_REG_HOSTCALL_STATUS:
		return npdisp_w2k_hostCallStatus;
	case NPDISP_W2K_REG_SYNC_STATUS:
		return npdisp_w2k_readSyncStatus();
	default:
		return 0;
	}
}

static void npdisp_w2k_writeRegister(UINT32 offset, UINT32 value)
{
	switch (offset) {
	case NPDISP_W2K_REG_WIDTH:
		npdisp_w2k_pendingWidth = value;
		break;
	case NPDISP_W2K_REG_HEIGHT:
		npdisp_w2k_pendingHeight = value;
		break;
	case NPDISP_W2K_REG_BPP:
		npdisp_w2k_pendingBpp = value;
		break;
	case NPDISP_W2K_REG_STRIDE:
		npdisp_w2k_pendingStride = value;
		break;
	case NPDISP_W2K_REG_MODE_COMMIT:
		if (value == NPDISP_W2K_MODE_RESET) npdisp_w2k_resetMode();
		else if (value == NPDISP_W2K_MODE_COMMIT) npdisp_w2k_commitMode();
		break;
	case NPDISP_W2K_REG_PAL_INDEX:
		npdisp_w2k_paletteIndex = value & 0xff;
		break;
	case NPDISP_W2K_REG_PAL_RGB:
		npdisp_w2k_paletteRgb = value & 0x00ffffffUL;
		break;
	case NPDISP_W2K_REG_HOSTCALL_ADDR:
		npdisp_w2k_hostCallAddr = value;
		npdisp_w2k_hostCallStatus = NPDISP_W2K_HOSTCALL_IDLE;
		break;
	case NPDISP_W2K_REG_HOSTCALL_CMD:
		npdisp_w2k_hostCall(value);
		break;
	case NPDISP_W2K_REG_PAL_COMMIT:
		if (npdisp.usePalette && npdisp_w2k_paletteIndex < 256) {
			npdisp_palette_rgb256[npdisp_w2k_paletteIndex].r = (UINT8)(npdisp_w2k_paletteRgb >> 16);
			npdisp_palette_rgb256[npdisp_w2k_paletteIndex].g = (UINT8)(npdisp_w2k_paletteRgb >> 8);
			npdisp_palette_rgb256[npdisp_w2k_paletteIndex].b = (UINT8)npdisp_w2k_paletteRgb;
			npdisp_palette_rgb256[npdisp_w2k_paletteIndex].reserved = 0;
			npdisp_palette_clearCache(npdisp_w2k_paletteIndex, npdisp_w2k_paletteIndex + 1);
			npdisp_setDirtyAll();
			npdisp.paletteUpdated = 1;
		}
		break;
	default:
		break;
	}
}

void npdisp_w2k_reset(void)
{
	npdisp_w2k_haxUnmap();
	npdisp.w2kLfb = 0;
	npdisp_w2k_pendingWidth = 640;
	npdisp_w2k_pendingHeight = 480;
	npdisp_w2k_pendingBpp = 8;
	npdisp_w2k_pendingStride = 640;
	npdisp_w2k_currentMode = 0;
	npdisp_w2k_paletteIndex = 0;
	npdisp_w2k_paletteRgb = 0;
	npdisp_w2k_hostCallAddr = 0;
	npdisp_w2k_hostCallStatus = NPDISP_W2K_HOSTCALL_IDLE;
	npdisp_w2k_traceReset();
}

void npdisp_w2k_restoreScreenMode(void)
{
	if (!npdisp.w2kLfb) return;
	npdisp_w2k_pendingWidth = npdisp.width;
	npdisp_w2k_pendingHeight = npdisp.height;
	npdisp_w2k_pendingBpp = npdisp.bpp;
	npdisp_w2k_pendingStride = (UINT32)npdispwin.stride;
	for (UINT32 i = 0; i < NPDISP_W2K_MODE_COUNT; ++i) {
		const NPDISP_W2K_MODE_INFO *mode = &npdisp_w2k_modes[i];
		if (mode->width == npdisp.width && mode->height == npdisp.height &&
			mode->bitsPerPixel == npdisp.bpp) {
			npdisp_w2k_currentMode = i;
			break;
		}
	}
}

void npdisp_w2k_saveState(NPDISP_W2K_STATE *state)
{
	if (!state) return;
	state->version = 1;
	state->pendingWidth = npdisp_w2k_pendingWidth;
	state->pendingHeight = npdisp_w2k_pendingHeight;
	state->pendingBpp = npdisp_w2k_pendingBpp;
	state->pendingStride = npdisp_w2k_pendingStride;
	state->currentMode = npdisp_w2k_currentMode;
	state->paletteIndex = npdisp_w2k_paletteIndex;
	state->paletteRgb = npdisp_w2k_paletteRgb;
	state->hostCallAddr = npdisp_w2k_hostCallAddr;
	state->hostCallStatus = npdisp_w2k_hostCallStatus;
}

int npdisp_w2k_loadState(const NPDISP_W2K_STATE *state)
{
	if (!state || state->version != 1 || state->currentMode >= NPDISP_W2K_MODE_COUNT ||
		state->paletteIndex > 255 || (state->paletteRgb & 0xff000000UL)) return 0;
	npdisp_w2k_pendingWidth = state->pendingWidth;
	npdisp_w2k_pendingHeight = state->pendingHeight;
	npdisp_w2k_pendingBpp = state->pendingBpp;
	npdisp_w2k_pendingStride = state->pendingStride;
	npdisp_w2k_currentMode = state->currentMode;
	npdisp_w2k_paletteIndex = state->paletteIndex;
	npdisp_w2k_paletteRgb = state->paletteRgb;
	npdisp_w2k_hostCallAddr = state->hostCallAddr;
	npdisp_w2k_hostCallStatus = state->hostCallStatus;
	return 1;
}

void npdisp_w2k_leaveMode(void)
{
	npdisp_w2k_haxUnmap();
	npdisp.w2kLfb = 0;
}

int npdisp_w2k_isActive(void)
{
	return npdisp.w2kLfb != 0;
}

int npdisp_w2k_memp_try_read8(UINT32 address, REG8 *value)
{
	UINT32 offset;
	UINT32 reg;

	if (!npdisp.ioenabled || !value) return 0;
	if (address < NPDISP_W2K_CTL_PHYS_BASE || address >= NPDISP_W2K_CTL_PHYS_BASE + NPDISP_W2K_CTL_SIZE) return 0;
	offset = address - NPDISP_W2K_CTL_PHYS_BASE;
	// All sync status values fit into the low byte.  Do not poll again for
	// the upper bytes of a split read.
	if ((offset & ~3UL) == NPDISP_W2K_REG_SYNC_STATUS && (offset & 3)) reg = 0;
	else reg = npdisp_w2k_readRegister(offset & ~3UL);
	*value = (REG8)(reg >> ((offset & 3) * 8));
	return 1;
}

int npdisp_w2k_memp_try_read16(UINT32 address, REG16 *value)
{
	REG8 b0;
	REG8 b1;

	if (!value) return 0;
	if (!npdisp_w2k_memp_try_read8(address, &b0)) return 0;
	if (!npdisp_w2k_memp_try_read8(address + 1, &b1)) return 0;
	*value = (REG16)(b0 | ((REG16)b1 << 8));
	return 1;
}

int npdisp_w2k_memp_try_read32(UINT32 address, UINT32 *value)
{
	REG8 b0;
	REG8 b1;
	REG8 b2;
	REG8 b3;

	if (!value) return 0;
	if (!npdisp_w2k_memp_try_read8(address, &b0)) return 0;
	if (!npdisp_w2k_memp_try_read8(address + 1, &b1)) return 0;
	if (!npdisp_w2k_memp_try_read8(address + 2, &b2)) return 0;
	if (!npdisp_w2k_memp_try_read8(address + 3, &b3)) return 0;
	*value = (UINT32)b0 | ((UINT32)b1 << 8) | ((UINT32)b2 << 16) | ((UINT32)b3 << 24);
	return 1;
}

int npdisp_w2k_memp_try_write8(UINT32 address, REG8 value)
{
	UINT32 offset;
	UINT32 shift;
	UINT32 reg;

	if (!npdisp.ioenabled) return 0;
	if (address < NPDISP_W2K_CTL_PHYS_BASE || address >= NPDISP_W2K_CTL_PHYS_BASE + NPDISP_W2K_CTL_SIZE) return 0;
	offset = address - NPDISP_W2K_CTL_PHYS_BASE;
	// Command registers are write-only and require one aligned 32-bit store.
	// Read/modify/write of a partial value must not execute a command.
	if ((offset & ~3UL) == NPDISP_W2K_REG_SYNC_STATUS ||
		(offset & ~3UL) == NPDISP_W2K_REG_MODE_COMMIT ||
		(offset & ~3UL) == NPDISP_W2K_REG_PAL_COMMIT ||
		(offset & ~3UL) == NPDISP_W2K_REG_HOSTCALL_CMD) return 1;
	shift = (offset & 3) * 8;
	reg = npdisp_w2k_readRegister(offset & ~3UL);
	reg = (reg & ~(0xffUL << shift)) | ((UINT32)value << shift);
	npdisp_w2k_writeRegister(offset & ~3UL, reg);
	return 1;
}

int npdisp_w2k_memp_try_write16(UINT32 address, REG16 value)
{
	if (!npdisp.ioenabled || address < NPDISP_W2K_CTL_PHYS_BASE ||
		address - NPDISP_W2K_CTL_PHYS_BASE > NPDISP_W2K_CTL_SIZE - 2) return 0;
	if (!npdisp_w2k_memp_try_write8(address, (REG8)value)) return 0;
	return npdisp_w2k_memp_try_write8(address + 1, (REG8)(value >> 8));
}

int npdisp_w2k_memp_try_write32(UINT32 address, UINT32 value)
{
	if (!npdisp.ioenabled || address < NPDISP_W2K_CTL_PHYS_BASE ||
		address - NPDISP_W2K_CTL_PHYS_BASE > NPDISP_W2K_CTL_SIZE - 4) return 0;
	if ((address & 3) == 0 && address >= NPDISP_W2K_CTL_PHYS_BASE &&
		address <= NPDISP_W2K_CTL_PHYS_BASE + NPDISP_W2K_CTL_SIZE - 4) {
		if (!npdisp.ioenabled) return 0;
		npdisp_w2k_writeRegister(address - NPDISP_W2K_CTL_PHYS_BASE, value);
		return 1;
	}
	if (!npdisp_w2k_memp_try_write16(address, (REG16)value)) return 0;
	return npdisp_w2k_memp_try_write16(address + 2, (REG16)(value >> 16));
}

#endif
