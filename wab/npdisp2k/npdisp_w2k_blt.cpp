/**
 * @file	npdisp_w2k_blt.cpp
 * @brief	NPDISP Windows 2000 BitBlt host implementation
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>

#include "pccore.h"
#include "../wab.h"

#include "../npdispdef.h"
#include "../npdisp.h"
#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"
#include "../npdisp_mem.h"
#include "npdisp_w2k_pattern.h"

extern NPDISP_WINDOWS npdispwin;

typedef struct {
	SINT32 left;
	SINT32 top;
	SINT32 right;
	SINT32 bottom;
} NPDISP_W2K_GUEST_RECTL;

typedef struct {
	SINT32 x;
	SINT32 y;
} NPDISP_W2K_GUEST_POINTL;

typedef struct {
	UINT32 dhsurf;
	UINT32 hsurf;
	UINT32 dhpdev;
	UINT32 hdev;
	SINT32 width;
	SINT32 height;
	UINT32 cjBits;
	UINT32 pvBits;
	UINT32 pvScan0;
	SINT32 lDelta;
	UINT32 iUniq;
	UINT32 iBitmapFormat;
	UINT16 iType;
	UINT16 fjBitmap;
} NPDISP_W2K_GUEST_SURFOBJ;

typedef struct {
	UINT32 iSolidColor;
	UINT32 pvRbrush;
	UINT32 flColorType;
} NPDISP_W2K_GUEST_BRUSHOBJ;

#define NPDISP_W2K_STYPE_BITMAP 0
#define NPDISP_W2K_BMF_8BPP     3
#define NPDISP_W2K_BMF_16BPP    4
#define NPDISP_W2K_BMF_24BPP    5
#define NPDISP_W2K_BMF_32BPP    6

static std::vector<UINT8> npdisp_w2k_bltScratch;
static std::vector<UINT8> npdisp_w2k_bltRowScratch;

static int npdisp_w2k_bltRopInfo(UINT32 rop4, UINT8 *rop3, int *usesSource, int *usesPattern)
{
	UINT8 fore;
	UINT8 back;

	if (!rop3 || !usesSource || !usesPattern) return 0;
	fore = (UINT8)(rop4 & 0xff);
	back = (UINT8)((rop4 >> 8) & 0xff);
	if (fore != back) return 0;
	*rop3 = fore;
	*usesSource = (((UINT8)(fore ^ (fore >> 2))) & 0x33) != 0;
	*usesPattern = (((UINT8)(fore ^ (fore >> 4))) & 0x0f) != 0;
	return 1;
}

static int npdisp_w2k_bltIntersect(NPDISP_W2K_RECT *dst, const NPDISP_W2K_RECT *a, const NPDISP_W2K_RECT *b)
{
	if (!dst || !a || !b) return 0;
	dst->left = (a->left > b->left) ? a->left : b->left;
	dst->top = (a->top > b->top) ? a->top : b->top;
	dst->right = (a->right < b->right) ? a->right : b->right;
	dst->bottom = (a->bottom < b->bottom) ? a->bottom : b->bottom;
	return dst->left < dst->right && dst->top < dst->bottom;
}

static int npdisp_w2k_bltBuildClipRects(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_BITBLT_REQUEST *request, const NPDISP_W2K_RECT *dstRect, std::vector<NPDISP_W2K_RECT> *clipRects)
{
	NPDISP_W2K_RECT screenRect;
	NPDISP_W2K_RECT clipRect;
	NPDISP_W2K_RECT clipped;
	std::vector<NPDISP_W2K_RECT> guestRects;
	UINT32 i;

	if (!request || !dstRect || !clipRects) return 0;
	clipRects->clear();
	screenRect.left = 0;
	screenRect.top = 0;
	screenRect.right = npdisp.width;
	screenRect.bottom = npdisp.height;

	if (request->clipType == NPDISP_W2K_CLIP_TRIVIAL) {
		if (npdisp_w2k_bltIntersect(&clipped, dstRect, &screenRect)) clipRects->push_back(clipped);
		return 1;
	}
	if (request->clipType == NPDISP_W2K_CLIP_RECT) {
		clipRect.left = request->clipBounds.left + request->dstOriginX;
		clipRect.top = request->clipBounds.top + request->dstOriginY;
		clipRect.right = request->clipBounds.right + request->dstOriginX;
		clipRect.bottom = request->clipBounds.bottom + request->dstOriginY;
		if (npdisp_w2k_bltIntersect(&clipped, dstRect, &clipRect) && npdisp_w2k_bltIntersect(&clipped, &clipped, &screenRect)) clipRects->push_back(clipped);
		return 1;
	}
	if (request->clipType != NPDISP_W2K_CLIP_COMPLEX || request->clipRectCount == 0 || request->clipRectCount > NPDISP_W2K_MAX_CLIP_RECTS) return 0;
	if (request->size < sizeof(*request) || request->clipRectCount > (request->size - sizeof(*request)) / sizeof(NPDISP_W2K_RECT)) return 0;
	guestRects.resize(request->clipRectCount);
	if (!npdisp_w2k_guestRead(requestAddr + sizeof(*request), &guestRects[0], request->clipRectCount * sizeof(NPDISP_W2K_RECT))) return 0;
	for (i = 0; i < request->clipRectCount; ++i) {
		clipRect.left = guestRects[i].left + request->dstOriginX;
		clipRect.top = guestRects[i].top + request->dstOriginY;
		clipRect.right = guestRects[i].right + request->dstOriginX;
		clipRect.bottom = guestRects[i].bottom + request->dstOriginY;
		if (npdisp_w2k_bltIntersect(&clipped, dstRect, &clipRect) && npdisp_w2k_bltIntersect(&clipped, &clipped, &screenRect)) clipRects->push_back(clipped);
	}
	return 1;
}

static UINT32 npdisp_w2k_bltPixelMask(void)
{
	if (npdisp.bpp == 8) return 0x000000ffUL;
	if (npdisp.bpp == 16) return 0x0000ffffUL;
	if (npdisp.bpp == 24) return 0x00ffffffUL;
	return 0xffffffffUL;
}

static UINT32 npdisp_w2k_bltEvalRop(UINT8 rop, UINT32 dst, UINT32 src, UINT32 pat, UINT32 mask)
{
	UINT32 result = 0;
	UINT32 i;

	for (i = 0; i < 8; ++i) {
		UINT32 term;
		if (!(rop & (1U << i))) continue;
		term = (i & 1) ? dst : ~dst;
		term &= (i & 2) ? src : ~src;
		term &= (i & 4) ? pat : ~pat;
		result |= term;
	}
	return result & mask;
}

static void npdisp_w2k_bltApply8(UINT8 *dst, const UINT8 *src, int count, UINT8 pat, UINT8 rop)
{
	int x;

	switch (rop) {
	case 0x00: memset(dst, 0x00, count); return;
	case 0x33: for (x = 0; x < count; ++x) dst[x] = (UINT8)~src[x]; return;
	case 0x44: for (x = 0; x < count; ++x) dst[x] = (UINT8)(src[x] & ~dst[x]); return;
	case 0x55: for (x = 0; x < count; ++x) dst[x] = (UINT8)~dst[x]; return;
	case 0x5a: for (x = 0; x < count; ++x) dst[x] ^= pat; return;
	case 0x66: for (x = 0; x < count; ++x) dst[x] ^= src[x]; return;
	case 0x88: for (x = 0; x < count; ++x) dst[x] &= src[x]; return;
	case 0xaa: return;
	case 0xbb: for (x = 0; x < count; ++x) dst[x] = (UINT8)((~src[x]) | dst[x]); return;
	case 0xcc: memcpy(dst, src, count); return;
	case 0xee: for (x = 0; x < count; ++x) dst[x] |= src[x]; return;
	case 0xf0: memset(dst, pat, count); return;
	case 0xff: memset(dst, 0xff, count); return;
	default:
		for (x = 0; x < count; ++x) dst[x] = (UINT8)npdisp_w2k_bltEvalRop(rop, dst[x], src ? src[x] : 0, pat, 0xff);
		return;
	}
}

static void npdisp_w2k_bltApply16(UINT16 *dst, const UINT16 *src, int count, UINT16 pat, UINT8 rop)
{
	int x;

	switch (rop) {
	case 0x00: for (x = 0; x < count; ++x) dst[x] = 0; return;
	case 0x33: for (x = 0; x < count; ++x) dst[x] = (UINT16)~src[x]; return;
	case 0x44: for (x = 0; x < count; ++x) dst[x] = (UINT16)(src[x] & ~dst[x]); return;
	case 0x55: for (x = 0; x < count; ++x) dst[x] = (UINT16)~dst[x]; return;
	case 0x5a: for (x = 0; x < count; ++x) dst[x] ^= pat; return;
	case 0x66: for (x = 0; x < count; ++x) dst[x] ^= src[x]; return;
	case 0x88: for (x = 0; x < count; ++x) dst[x] &= src[x]; return;
	case 0xaa: return;
	case 0xbb: for (x = 0; x < count; ++x) dst[x] = (UINT16)((~src[x]) | dst[x]); return;
	case 0xcc: memcpy(dst, src, (UINT32)count * 2); return;
	case 0xee: for (x = 0; x < count; ++x) dst[x] |= src[x]; return;
	case 0xf0: for (x = 0; x < count; ++x) dst[x] = pat; return;
	case 0xff: for (x = 0; x < count; ++x) dst[x] = 0xffff; return;
	default:
		for (x = 0; x < count; ++x) dst[x] = (UINT16)npdisp_w2k_bltEvalRop(rop, dst[x], src ? src[x] : 0, pat, 0xffff);
		return;
	}
}

static UINT32 npdisp_w2k_bltRead24(const UINT8 *p)
{
	return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16);
}

static void npdisp_w2k_bltWrite24(UINT8 *p, UINT32 value)
{
	p[0] = (UINT8)value;
	p[1] = (UINT8)(value >> 8);
	p[2] = (UINT8)(value >> 16);
}

static void npdisp_w2k_bltApply24(UINT8 *dst, const UINT8 *src, int count, UINT32 pat, UINT8 rop)
{
	int x;

	pat &= 0x00ffffffUL;
	switch (rop) {
	case 0x00: memset(dst, 0x00, (UINT32)count * 3); return;
	case 0x55:
		for (x = 0; x < count * 3; ++x) dst[x] = (UINT8)~dst[x];
		return;
	case 0xaa: return;
	case 0xcc: memcpy(dst, src, (UINT32)count * 3); return;
	case 0xff: memset(dst, 0xff, (UINT32)count * 3); return;
	default:
		for (x = 0; x < count; ++x) {
			UINT32 d = npdisp_w2k_bltRead24(dst + x * 3);
			UINT32 sv = src ? npdisp_w2k_bltRead24(src + x * 3) : 0;
			UINT32 result;
			switch (rop) {
			case 0x33: result = ~sv; break;
			case 0x44: result = sv & ~d; break;
			case 0x5a: result = d ^ pat; break;
			case 0x66: result = d ^ sv; break;
			case 0x88: result = d & sv; break;
			case 0xbb: result = (~sv) | d; break;
			case 0xee: result = d | sv; break;
			case 0xf0: result = pat; break;
			default: result = npdisp_w2k_bltEvalRop(rop, d, sv, pat, 0x00ffffffUL); break;
			}
			npdisp_w2k_bltWrite24(dst + x * 3, result);
		}
		return;
	}
}

static void npdisp_w2k_bltApply32(UINT32 *dst, const UINT32 *src, int count, UINT32 pat, UINT8 rop)
{
	int x;

	switch (rop) {
	case 0x00: memset(dst, 0x00, (UINT32)count * 4); return;
	case 0x33: for (x = 0; x < count; ++x) dst[x] = ~src[x]; return;
	case 0x44: for (x = 0; x < count; ++x) dst[x] = src[x] & ~dst[x]; return;
	case 0x55: for (x = 0; x < count; ++x) dst[x] = ~dst[x]; return;
	case 0x5a: for (x = 0; x < count; ++x) dst[x] ^= pat; return;
	case 0x66: for (x = 0; x < count; ++x) dst[x] ^= src[x]; return;
	case 0x88: for (x = 0; x < count; ++x) dst[x] &= src[x]; return;
	case 0xaa: return;
	case 0xbb: for (x = 0; x < count; ++x) dst[x] = (~src[x]) | dst[x]; return;
	case 0xcc: memcpy(dst, src, (UINT32)count * 4); return;
	case 0xee: for (x = 0; x < count; ++x) dst[x] |= src[x]; return;
	case 0xf0: for (x = 0; x < count; ++x) dst[x] = pat; return;
	case 0xff: memset(dst, 0xff, (UINT32)count * 4); return;
	default:
		for (x = 0; x < count; ++x) dst[x] = npdisp_w2k_bltEvalRop(rop, dst[x], src ? src[x] : 0, pat, 0xffffffffUL);
		return;
	}
}

static void npdisp_w2k_bltApplyRow(UINT8 *dst, const UINT8 *src, int count, UINT32 pat, UINT8 rop)
{
	if (npdisp.bpp == 8) npdisp_w2k_bltApply8(dst, src, count, (UINT8)pat, rop);
	else if (npdisp.bpp == 16) npdisp_w2k_bltApply16((UINT16 *)dst, (const UINT16 *)src, count, (UINT16)pat, rop);
	else if (npdisp.bpp == 24) npdisp_w2k_bltApply24(dst, src, count, pat, rop);
	else npdisp_w2k_bltApply32((UINT32 *)dst, (const UINT32 *)src, count, pat, rop);
}

static UINT32 npdisp_w2k_bltReadPixel(const UINT8 *p)
{
	if (npdisp.bpp == 8) return p[0];
	if (npdisp.bpp == 16) return (UINT32)p[0] | ((UINT32)p[1] << 8);
	if (npdisp.bpp == 24) return npdisp_w2k_bltRead24(p);
	return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static void npdisp_w2k_bltWritePixel(UINT8 *p, UINT32 value)
{
	if (npdisp.bpp == 8) p[0] = (UINT8)value;
	else if (npdisp.bpp == 16) { p[0] = (UINT8)value; p[1] = (UINT8)(value >> 8); }
	else if (npdisp.bpp == 24) npdisp_w2k_bltWrite24(p, value);
	else { p[0] = (UINT8)value; p[1] = (UINT8)(value >> 8); p[2] = (UINT8)(value >> 16); p[3] = (UINT8)(value >> 24); }
}

static void npdisp_w2k_bltApplyPatternRow(UINT8 *dst, const UINT8 *src, int count, UINT32 bytesPerPixel, const NPDISP_W2K_PATTERN *pattern, int startX, int y, int originX, int originY, UINT8 rop, UINT32 pixelMask)
{
	int x;

	for (x = 0; x < count; ++x) {
		UINT8 *d = dst + (UINT32)x * bytesPerPixel;
		const UINT8 *s = src ? src + (UINT32)x * bytesPerPixel : NULL;
		UINT32 pat = npdisp_w2k_patternPixel(pattern, startX + x, y, originX, originY);
		UINT32 sv = s ? npdisp_w2k_bltReadPixel(s) : 0;
		UINT32 dv;
		UINT32 result;
		if (rop == 0xf0) result = pat;
		else {
			dv = npdisp_w2k_bltReadPixel(d);
			if (rop == 0x5a) result = dv ^ pat;
			else result = npdisp_w2k_bltEvalRop(rop, dv, sv, pat, pixelMask);
		}
		npdisp_w2k_bltWritePixel(d, result & pixelMask);
	}
}

static int npdisp_w2k_bltSurfaceFormat(const NPDISP_W2K_GUEST_SURFOBJ *surface)
{
	UINT32 expectedFormat;

	if (!surface || surface->iType != NPDISP_W2K_STYPE_BITMAP || surface->pvScan0 == 0 || surface->width <= 0 || surface->height <= 0 || surface->lDelta == 0) return 0;
	if (npdisp.bpp == 8) expectedFormat = NPDISP_W2K_BMF_8BPP;
	else if (npdisp.bpp == 16) expectedFormat = NPDISP_W2K_BMF_16BPP;
	else if (npdisp.bpp == 24) expectedFormat = NPDISP_W2K_BMF_24BPP;
	else if (npdisp.bpp == 32) expectedFormat = NPDISP_W2K_BMF_32BPP;
	else return 0;
	return surface->iBitmapFormat == expectedFormat;
}

static int npdisp_w2k_bltMemoryRowAddr(const NPDISP_W2K_GUEST_SURFOBJ *surface, int x, int y, UINT32 rowBytes, UINT32 bytesPerPixel, UINT32 *rowAddr)
{
	SINT64 address;

	if (!surface || !rowAddr || x < 0 || y < 0 || x > surface->width || y >= surface->height) return 0;
	if ((UINT32)x > 0xffffffffUL / bytesPerPixel) return 0;
	address = (SINT64)(UINT32)surface->pvScan0 + (SINT64)y * surface->lDelta + (SINT64)(UINT32)x * bytesPerPixel;
	if (address < 0 || address > 0xffffffffLL || (UINT64)address + rowBytes > 0x100000000ULL) return 0;
	*rowAddr = (UINT32)address;
	return 1;
}

static int npdisp_w2k_bltRectOverlap(const NPDISP_W2K_RECT *a, const NPDISP_W2K_RECT *b)
{
	return a && b && a->left < b->right && b->left < a->right && a->top < b->bottom && b->top < a->bottom;
}

static int npdisp_w2k_bltValidateMemoryAddresses(const std::vector<NPDISP_W2K_RECT> *clipRects, const NPDISP_W2K_GUEST_SURFOBJ *surface, int dstX, int dstY, int srcX, int srcY, UINT32 bytesPerPixel)
{
	size_t i;

	if (!clipRects || !surface) return 0;
	for (i = 0; i < clipRects->size(); ++i) {
		int sx = srcX + (*clipRects)[i].left - dstX;
		int sy = srcY + (*clipRects)[i].top - dstY;
		int width = (*clipRects)[i].right - (*clipRects)[i].left;
		int height = (*clipRects)[i].bottom - (*clipRects)[i].top;
		UINT32 rowBytes = (UINT32)width * bytesPerPixel;
		UINT32 rowAddr;
		if (!npdisp_w2k_bltMemoryRowAddr(surface, sx, sy, rowBytes, bytesPerPixel, &rowAddr)) return 0;
		UINT32 lastAddr;
		if (height > 1 && !npdisp_w2k_bltMemoryRowAddr(surface, sx, sy + height - 1, rowBytes, bytesPerPixel, &lastAddr)) return 0;
#if defined(CPUCORE_IA32)
		// Preflight all source pages before changing the destination (non-idempotent ROPs).
		if (!npdisp_w2k_checkLinearRows(rowAddr, surface->lDelta, rowBytes, (UINT32)height, 0)) return 0;
#else
		return 0;
#endif
	}
	return 1;
}

static int npdisp_w2k_bltValidateSourceRects(const std::vector<NPDISP_W2K_RECT> *clipRects, int dstX, int dstY, int srcX, int srcY, int srcWidth, int srcHeight)
{
	size_t i;

	if (!clipRects) return 0;
	for (i = 0; i < clipRects->size(); ++i) {
		int sx = srcX + (*clipRects)[i].left - dstX;
		int sy = srcY + (*clipRects)[i].top - dstY;
		int w = (*clipRects)[i].right - (*clipRects)[i].left;
		int h = (*clipRects)[i].bottom - (*clipRects)[i].top;
		if (sx < 0 || sy < 0 || w <= 0 || h <= 0 || sx > srcWidth - w || sy > srcHeight - h) return 0;
	}
	return 1;
}

static void npdisp_w2k_bltCopyScreenRect(const NPDISP_W2K_RECT *rect, int dstX, int dstY, int srcX, int srcY, UINT32 bytesPerPixel)
{
	UINT8 *bits = (UINT8 *)npdispwin.pBits;
	int sx;
	int sy;
	int width;
	int height;
	int first;
	int last;
	int step;
	int y;
	UINT32 rowBytes;

	if (!rect) return;
	sx = srcX + rect->left - dstX;
	sy = srcY + rect->top - dstY;
	width = rect->right - rect->left;
	height = rect->bottom - rect->top;
	rowBytes = (UINT32)width * bytesPerPixel;
	if (rect->top > sy && rect->top < sy + height) {
		first = height - 1;
		last = -1;
		step = -1;
	}
	else {
		first = 0;
		last = height;
		step = 1;
	}
	for (y = first; y != last; y += step) {
		UINT8 *dst = bits + (UINT32)(rect->top + y) * npdispwin.stride + (UINT32)rect->left * bytesPerPixel;
		UINT8 *src = bits + (UINT32)(sy + y) * npdispwin.stride + (UINT32)sx * bytesPerPixel;
		memmove(dst, src, rowBytes);
	}
}

static int npdisp_w2k_bltSnapshotScreen(const std::vector<NPDISP_W2K_RECT> *clipRects, int dstX, int dstY, int srcX, int srcY, UINT32 bytesPerPixel, NPDISP_W2K_RECT *snapshotRect)
{
	NPDISP_W2K_RECT bounds;
	UINT32 rowBytes;
	UINT64 totalBytes;
	int sourceX;
	int sourceY;
	int width;
	int height;
	int y;
	size_t i;

	if (!clipRects || clipRects->empty() || !snapshotRect || !npdispwin.pBits) return 0;
	bounds = (*clipRects)[0];
	for (i = 1; i < clipRects->size(); ++i) {
		if ((*clipRects)[i].left < bounds.left) bounds.left = (*clipRects)[i].left;
		if ((*clipRects)[i].top < bounds.top) bounds.top = (*clipRects)[i].top;
		if ((*clipRects)[i].right > bounds.right) bounds.right = (*clipRects)[i].right;
		if ((*clipRects)[i].bottom > bounds.bottom) bounds.bottom = (*clipRects)[i].bottom;
	}
	width = bounds.right - bounds.left;
	height = bounds.bottom - bounds.top;
	sourceX = srcX + bounds.left - dstX;
	sourceY = srcY + bounds.top - dstY;
	if (width <= 0 || height <= 0 || sourceX < 0 || sourceY < 0 || sourceX > npdisp.width - width || sourceY > npdisp.height - height) return 0;
	rowBytes = (UINT32)width * bytesPerPixel;
	totalBytes = (UINT64)rowBytes * (UINT32)height;
	if (totalBytes > 0xffffffffUL) return 0;
	npdisp_w2k_bltScratch.resize((size_t)totalBytes);
	for (y = 0; y < height; ++y) {
		memcpy(&npdisp_w2k_bltScratch[(size_t)y * rowBytes], (UINT8 *)npdispwin.pBits + (UINT32)(sourceY + y) * npdispwin.stride + (UINT32)sourceX * bytesPerPixel, rowBytes);
	}
	*snapshotRect = bounds;
	return 1;
}

static UINT64 npdisp_w2k_bltWorkPixels(const std::vector<NPDISP_W2K_RECT> *clipRects)
{
	UINT64 pixels = 0;
	size_t i;

	if (!clipRects) return 0;
	for (i = 0; i < clipRects->size(); ++i) {
		SINT32 width = (*clipRects)[i].right - (*clipRects)[i].left;
		SINT32 height = (*clipRects)[i].bottom - (*clipRects)[i].top;
		if (width > 0 && height > 0) pixels += (UINT64)(UINT32)width * (UINT32)height;
	}
	return pixels;
}

static void npdisp_w2k_bltUpdateDirty(const std::vector<NPDISP_W2K_RECT> *clipRects)
{
	NPDISP_W2K_RECT dirty;
	size_t i;

	if (!clipRects || clipRects->empty()) return;
	dirty = (*clipRects)[0];
	for (i = 1; i < clipRects->size(); ++i) {
		if ((*clipRects)[i].left < dirty.left) dirty.left = (*clipRects)[i].left;
		if ((*clipRects)[i].top < dirty.top) dirty.top = (*clipRects)[i].top;
		if ((*clipRects)[i].right > dirty.right) dirty.right = (*clipRects)[i].right;
		if ((*clipRects)[i].bottom > dirty.bottom) dirty.bottom = (*clipRects)[i].bottom;
	}
	npdisp_setDirty(dirty.left, dirty.top, dirty.right, dirty.bottom);
	npdisp.updated = 1;
}

UINT32 npdisp_w2k_hostCallBitBlt(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_BITBLT_REQUEST request;
	NPDISP_W2K_GUEST_RECTL guestDstRect;
	NPDISP_W2K_GUEST_POINTL srcPoint;
	NPDISP_W2K_GUEST_SURFOBJ srcSurface;
	NPDISP_W2K_GUEST_BRUSHOBJ brush;
	NPDISP_W2K_GUEST_POINTL brushPoint;
	NPDISP_W2K_PATTERN realizedPattern;
	NPDISP_W2K_RECT dstRect;
	NPDISP_W2K_RECT srcScreenRect;
	NPDISP_W2K_RECT snapshotRect;
	std::vector<NPDISP_W2K_RECT> clipRects;
	UINT8 rop;
	int usesSource;
	int usesPattern;
	int srcIsScreen = 0;
	int srcIsMemory = 0;
	int sourceOverlap = 0;
	int useSnapshot = 0;
	int useRealizedPattern = 0;
	int patternOriginX = 0;
	int patternOriginY = 0;
	int dstX;
	int dstY;
	int srcX = 0;
	int srcY = 0;
	int width;
	int height;
	UINT32 bytesPerPixel;
	UINT32 pattern = 0;
	UINT32 pixelMask;
	size_t i;

	if (!npdisp.w2kLfb || !npdisp.enabled || !npdispwin.pBits || npdispwin.stride == 0) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (npdisp.bpp != 8 && npdisp.bpp != 16 && npdisp.bpp != 24 && npdisp.bpp != 32) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size < sizeof(request) || (UINT64)requestAddr + request.size > 0x100000000ULL || request.command != NPDISP_W2K_HOSTCALL_BITBLT ||
		!(request.surfaceFlags & NPDISP_W2K_BITBLT_DST_SCREEN) || request.dstSurfaceAddr == 0 || request.dstRectAddr == 0 ||
		!npdisp_w2k_bltRopInfo(request.rop4, &rop, &usesSource, &usesPattern)) {
		return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	}

	if (!npdisp_w2k_guestRead(request.dstRectAddr, &guestDstRect, sizeof(guestDstRect))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	width = guestDstRect.right - guestDstRect.left;
	height = guestDstRect.bottom - guestDstRect.top;
	if (width <= 0 || height <= 0) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	dstX = guestDstRect.left + request.dstOriginX;
	dstY = guestDstRect.top + request.dstOriginY;
	dstRect.left = dstX;
	dstRect.top = dstY;
	dstRect.right = dstX + width;
	dstRect.bottom = dstY + height;
	if (!npdisp_w2k_bltBuildClipRects(requestAddr, &request, &dstRect, &clipRects)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (clipRects.empty() || rop == 0xaa) return NPDISP_W2K_HOSTCALL_OK;

	bytesPerPixel = npdisp.bpp >> 3;
	pixelMask = npdisp_w2k_bltPixelMask();

	if (usesPattern) {
		if (!request.brushObjAddr) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		if (!npdisp_w2k_guestRead(request.brushObjAddr, &brush, sizeof(brush))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if (brush.iSolidColor == 0xffffffffUL) {
			if (!npdisp_w2k_patternLoad(request.brushObjAddr, &realizedPattern)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
			useRealizedPattern = 1;
			if (request.brushPointAddr) {
				if (!npdisp_w2k_guestRead(request.brushPointAddr, &brushPoint, sizeof(brushPoint))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
				patternOriginX = brushPoint.x + request.dstOriginX;
				patternOriginY = brushPoint.y + request.dstOriginY;
			}
			else {
				patternOriginX = request.dstOriginX;
				patternOriginY = request.dstOriginY;
			}
		}
		else pattern = brush.iSolidColor & pixelMask;
	}

	if (usesSource) {
		if (!request.srcSurfaceAddr || !request.srcPointAddr || !(request.surfaceFlags & NPDISP_W2K_BITBLT_XLATE_TRIVIAL)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		if (!npdisp_w2k_guestRead(request.srcPointAddr, &srcPoint, sizeof(srcPoint))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if (request.surfaceFlags & NPDISP_W2K_BITBLT_SRC_SCREEN) {
			srcIsScreen = 1;
			srcX = srcPoint.x + request.srcOriginX;
			srcY = srcPoint.y + request.srcOriginY;
			if (!npdisp_w2k_bltValidateSourceRects(&clipRects, dstX, dstY, srcX, srcY, npdisp.width, npdisp.height)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
			srcScreenRect.left = srcX;
			srcScreenRect.top = srcY;
			srcScreenRect.right = srcX + width;
			srcScreenRect.bottom = srcY + height;
			sourceOverlap = npdisp_w2k_bltRectOverlap(&dstRect, &srcScreenRect);
		}
		else {
			srcIsMemory = 1;
			if (!npdisp_w2k_guestRead(request.srcSurfaceAddr, &srcSurface, sizeof(srcSurface))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
			if (!npdisp_w2k_bltSurfaceFormat(&srcSurface)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
			srcX = srcPoint.x;
			srcY = srcPoint.y;
			if (!npdisp_w2k_bltValidateSourceRects(&clipRects, dstX, dstY, srcX, srcY, srcSurface.width, srcSurface.height)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
			if (!npdisp_w2k_bltValidateMemoryAddresses(&clipRects, &srcSurface, dstX, dstY, srcX, srcY, bytesPerPixel)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		}
	}

	if (srcIsScreen && rop == 0xcc && clipRects.size() == 1 && !useRealizedPattern) {
		UINT64 workPixels = npdisp_w2k_bltWorkPixels(&clipRects);
		npdisp_w2k_bltCopyScreenRect(&clipRects[0], dstX, dstY, srcX, srcY, bytesPerPixel);
		npdisp_w2k_bltUpdateDirty(&clipRects);
		npdisp_w2k_chargePixels(workPixels);
		npdisp_w2k_traceBitBltClass(NPDISP_W2K_BITBLT_TRACE_S2S, workPixels, 0, 0);
		return NPDISP_W2K_HOSTCALL_OK;
	}

	if (srcIsScreen && sourceOverlap) {
		if (!npdisp_w2k_bltSnapshotScreen(&clipRects, dstX, dstY, srcX, srcY, bytesPerPixel, &snapshotRect)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		useSnapshot = 1;
	}

	for (i = 0; i < clipRects.size(); ++i) {
		const NPDISP_W2K_RECT *rect = &clipRects[i];
		int rectWidth = rect->right - rect->left;
		int rectHeight = rect->bottom - rect->top;
		int sourceRectX = srcX + rect->left - dstX;
		int sourceRectY = srcY + rect->top - dstY;
		UINT32 rowBytes = (UINT32)rectWidth * bytesPerPixel;
		int y;

		if (srcIsMemory && rop == 0xcc && !useRealizedPattern) {
			UINT32 rowAddr;
			UINT8 *dst = (UINT8 *)npdispwin.pBits + (UINT32)rect->top * npdispwin.stride + (UINT32)rect->left * bytesPerPixel;
			if (!npdisp_w2k_bltMemoryRowAddr(&srcSurface, sourceRectX, sourceRectY, rowBytes, bytesPerPixel, &rowAddr)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
			if (!npdisp_w2k_guestReadStrided(rowAddr, srcSurface.lDelta, dst, npdispwin.stride, rowBytes, (UINT32)rectHeight)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
			npdisp_w2k_traceBitBltBatch(1, (UINT64)rowBytes * (UINT32)rectHeight);
			continue;
		}

		if (srcIsMemory) npdisp_w2k_bltRowScratch.resize(rowBytes);
		for (y = 0; y < rectHeight; ++y) {
			UINT8 *dst = (UINT8 *)npdispwin.pBits + (UINT32)(rect->top + y) * npdispwin.stride + (UINT32)rect->left * bytesPerPixel;
			const UINT8 *src = NULL;
			if (srcIsScreen) {
				if (useSnapshot) {
					UINT32 snapshotStride = (UINT32)(snapshotRect.right - snapshotRect.left) * bytesPerPixel;
					src = &npdisp_w2k_bltScratch[(size_t)(rect->top - snapshotRect.top + y) * snapshotStride + (size_t)(rect->left - snapshotRect.left) * bytesPerPixel];
				}
				else {
					src = (UINT8 *)npdispwin.pBits + (UINT32)(sourceRectY + y) * npdispwin.stride + (UINT32)sourceRectX * bytesPerPixel;
				}
			}
			else if (srcIsMemory) {
				UINT32 rowAddr;
				if (!npdisp_w2k_bltMemoryRowAddr(&srcSurface, sourceRectX, sourceRectY + y, rowBytes, bytesPerPixel, &rowAddr)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
				if (!npdisp_w2k_guestRead(rowAddr, &npdisp_w2k_bltRowScratch[0], rowBytes)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
				src = &npdisp_w2k_bltRowScratch[0];
			}
			if (useRealizedPattern) npdisp_w2k_bltApplyPatternRow(dst, src, rectWidth, bytesPerPixel, &realizedPattern, rect->left, rect->top + y, patternOriginX, patternOriginY, rop, pixelMask);
			else npdisp_w2k_bltApplyRow(dst, src, rectWidth, pattern, rop);
		}
	}

	{
		UINT64 workPixels = npdisp_w2k_bltWorkPixels(&clipRects);
		UINT32 traceClass = srcIsScreen ? NPDISP_W2K_BITBLT_TRACE_S2S : (srcIsMemory ? NPDISP_W2K_BITBLT_TRACE_M2S : NPDISP_W2K_BITBLT_TRACE_FILL);
		UINT64 readBytes = srcIsMemory ? workPixels * bytesPerPixel : 0;
		npdisp_w2k_bltUpdateDirty(&clipRects);
		npdisp_w2k_chargePixels(workPixels);
		npdisp_w2k_traceBitBltClass(traceClass, workPixels, readBytes, useRealizedPattern);
	}
	return NPDISP_W2K_HOSTCALL_OK;
}

#endif
