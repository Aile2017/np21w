/**
 * @file	npdisp_w2k_stretch.cpp
 * @brief	NPDISP Windows 2000 StretchBlt host implementation
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

extern NPDISP_WINDOWS npdispwin;

typedef struct {
	SINT32 left;
	SINT32 top;
	SINT32 right;
	SINT32 bottom;
} NPDISP_W2K_GUEST_RECTL;

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
	int valid;
	SINT32 left;
	SINT32 top;
	SINT32 right;
	SINT32 bottom;
	UINT64 workPixels;
} NPDISP_W2K_STRETCH_DIRTY;

#define NPDISP_W2K_STYPE_BITMAP 0
#define NPDISP_W2K_BMF_8BPP     3
#define NPDISP_W2K_BMF_16BPP    4
#define NPDISP_W2K_BMF_24BPP    5
#define NPDISP_W2K_BMF_32BPP    6
#define NPDISP_W2K_COLORONCOLOR  3

static int npdisp_w2k_stretchIntersect(NPDISP_W2K_RECT *dst, const NPDISP_W2K_RECT *a, const NPDISP_W2K_RECT *b)
{
	if (!dst || !a || !b) return 0;
	dst->left = (a->left > b->left) ? a->left : b->left;
	dst->top = (a->top > b->top) ? a->top : b->top;
	dst->right = (a->right < b->right) ? a->right : b->right;
	dst->bottom = (a->bottom < b->bottom) ? a->bottom : b->bottom;
	return dst->left < dst->right && dst->top < dst->bottom;
}

static int npdisp_w2k_stretchBuildClipRects(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request, const NPDISP_W2K_RECT *dstRect, std::vector<NPDISP_W2K_RECT> *clipRects)
{
	NPDISP_W2K_RECT screenRect;
	NPDISP_W2K_RECT clipRect;
	NPDISP_W2K_RECT clipped;
	std::vector<NPDISP_W2K_RECT> guestRects;
	UINT64 clipBytes;
	UINT32 i;

	if (!request || !dstRect || !clipRects) return 0;
	clipRects->clear();
	screenRect.left = 0;
	screenRect.top = 0;
	screenRect.right = npdisp.width;
	screenRect.bottom = npdisp.height;
	if (request->clipType == NPDISP_W2K_CLIP_TRIVIAL) {
		if (npdisp_w2k_stretchIntersect(&clipped, dstRect, &screenRect)) clipRects->push_back(clipped);
		return 1;
	}
	if (request->clipType == NPDISP_W2K_CLIP_RECT) {
		clipRect.left = request->clipBounds.left + request->dstOriginX;
		clipRect.top = request->clipBounds.top + request->dstOriginY;
		clipRect.right = request->clipBounds.right + request->dstOriginX;
		clipRect.bottom = request->clipBounds.bottom + request->dstOriginY;
		if (npdisp_w2k_stretchIntersect(&clipped, dstRect, &clipRect) &&
			npdisp_w2k_stretchIntersect(&clipped, &clipped, &screenRect)) clipRects->push_back(clipped);
		return 1;
	}
	if (request->clipType != NPDISP_W2K_CLIP_COMPLEX || request->clipRectCount > NPDISP_W2K_MAX_CLIP_RECTS) return 0;
	if (request->clipRectCount == 0) return 1;
	clipBytes = (UINT64)request->clipRectCount * sizeof(NPDISP_W2K_RECT);
	if (request->clipRectOffset < sizeof(*request) || request->clipRectOffset > request->size || clipBytes > request->size - request->clipRectOffset) return 0;
	guestRects.resize(request->clipRectCount);
	npdisp_w2k_guestRead(requestAddr + request->clipRectOffset, &guestRects[0], (UINT32)clipBytes);
	for (i = 0; i < request->clipRectCount; ++i) {
		clipRect.left = guestRects[i].left + request->dstOriginX;
		clipRect.top = guestRects[i].top + request->dstOriginY;
		clipRect.right = guestRects[i].right + request->dstOriginX;
		clipRect.bottom = guestRects[i].bottom + request->dstOriginY;
		if (npdisp_w2k_stretchIntersect(&clipped, dstRect, &clipRect) &&
			npdisp_w2k_stretchIntersect(&clipped, &clipped, &screenRect)) clipRects->push_back(clipped);
	}
	return 1;
}

static UINT32 npdisp_w2k_stretchBytesPerPixel(void)
{
	if (npdisp.bpp == 8) return 1;
	if (npdisp.bpp == 16) return 2;
	if (npdisp.bpp == 24) return 3;
	if (npdisp.bpp == 32) return 4;
	return 0;
}

static UINT32 npdisp_w2k_stretchPixelMask(void)
{
	if (npdisp.bpp == 8) return 0x000000ffUL;
	if (npdisp.bpp == 16) return 0x0000ffffUL;
	if (npdisp.bpp == 24) return 0x00ffffffUL;
	return 0xffffffffUL;
}

static UINT32 npdisp_w2k_stretchReadPixel(const UINT8 *p)
{
	if (npdisp.bpp == 8) return p[0];
	if (npdisp.bpp == 16) return *(const UINT16 *)p;
	if (npdisp.bpp == 24) return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16);
	return *(const UINT32 *)p;
}

static void npdisp_w2k_stretchWritePixel(UINT8 *p, UINT32 value)
{
	if (npdisp.bpp == 8) p[0] = (UINT8)value;
	else if (npdisp.bpp == 16) *(UINT16 *)p = (UINT16)value;
	else if (npdisp.bpp == 24) {
		p[0] = (UINT8)value;
		p[1] = (UINT8)(value >> 8);
		p[2] = (UINT8)(value >> 16);
	}
	else *(UINT32 *)p = value;
}

static UINT32 npdisp_w2k_stretchEvalRop(UINT8 rop, UINT32 dst, UINT32 src, UINT32 pat, UINT32 mask)
{
	UINT32 result = 0;
	UINT32 i;

	switch (rop) {
	case 0x00: return 0;
	case 0x33: return (~src) & mask;
	case 0x44: return (src & ~dst) & mask;
	case 0x55: return (~dst) & mask;
	case 0x5a: return (dst ^ pat) & mask;
	case 0x66: return (dst ^ src) & mask;
	case 0x88: return (dst & src) & mask;
	case 0xaa: return dst & mask;
	case 0xbb: return ((~src) | dst) & mask;
	case 0xcc: return src & mask;
	case 0xee: return (dst | src) & mask;
	case 0xf0: return pat & mask;
	case 0xff: return mask;
	default:
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
}

static int npdisp_w2k_stretchRopInfo(UINT32 rop4, UINT8 *rop3, int *usesSource, int *usesPattern)
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

static int npdisp_w2k_stretchSurfaceFormat(const NPDISP_W2K_GUEST_SURFOBJ *surface)
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

static int npdisp_w2k_stretchMemoryRowAddr(const NPDISP_W2K_GUEST_SURFOBJ *surface, int x, int y, UINT32 rowBytes, UINT32 bytesPerPixel, UINT32 *rowAddr)
{
	SINT64 address;

	if (!surface || !rowAddr || x < 0 || y < 0 || x > surface->width || y >= surface->height) return 0;
	if ((UINT32)x > 0xffffffffUL / bytesPerPixel) return 0;
	address = (SINT64)(UINT32)surface->pvScan0 + (SINT64)y * surface->lDelta + (SINT64)(UINT32)x * bytesPerPixel;
	if (address < 0 || address > 0xffffffffLL || (UINT64)address + rowBytes > 0x100000000ULL) return 0;
	*rowAddr = (UINT32)address;
	return 1;
}

static int npdisp_w2k_stretchSnapshotSource(const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request, const NPDISP_W2K_RECT *srcRect, UINT32 bytesPerPixel, std::vector<UINT8> *source, UINT32 *sourceStride)
{
	NPDISP_W2K_GUEST_SURFOBJ surface;
	int width;
	int height;
	UINT32 rowBytes;
	UINT64 totalBytes;
	int y;

	if (!request || !srcRect || !source || !sourceStride) return 0;
	width = srcRect->right - srcRect->left;
	height = srcRect->bottom - srcRect->top;
	if (width <= 0 || height <= 0 || (UINT32)width > 0xffffffffUL / bytesPerPixel) return 0;
	rowBytes = (UINT32)width * bytesPerPixel;
	totalBytes = (UINT64)rowBytes * (UINT32)height;
	if (totalBytes == 0 || totalBytes > NPDISP_W2K_FB_SIZE) return 0;
	source->resize((size_t)totalBytes);
	*sourceStride = rowBytes;
	if (request->surfaceFlags & NPDISP_W2K_GDI_SRC_SCREEN) {
		if (srcRect->left < 0 || srcRect->top < 0 || srcRect->right > npdisp.width || srcRect->bottom > npdisp.height) return 0;
		for (y = 0; y < height; ++y) {
			memcpy(&(*source)[(size_t)y * rowBytes], (UINT8 *)npdispwin.pBits + (UINT32)(srcRect->top + y) * npdispwin.stride + (UINT32)srcRect->left * bytesPerPixel, rowBytes);
		}
		return 1;
	}
	if (request->arg[1] == 0) return 0;
	npdisp_w2k_guestRead(request->arg[1], &surface, sizeof(surface));
	if (!npdisp_w2k_stretchSurfaceFormat(&surface)) return 0;
	if (srcRect->left < 0 || srcRect->top < 0 || srcRect->right > surface.width || srcRect->bottom > surface.height) return 0;
	for (y = 0; y < height; ++y) {
		UINT32 rowAddr;
		if (!npdisp_w2k_stretchMemoryRowAddr(&surface, srcRect->left, srcRect->top + y, rowBytes, bytesPerPixel, &rowAddr)) return 0;
		npdisp_w2k_guestRead(rowAddr, &(*source)[(size_t)y * rowBytes], rowBytes);
	}
	return 1;
}

static void npdisp_w2k_stretchMarkDirty(NPDISP_W2K_STRETCH_DIRTY *dirty, const NPDISP_W2K_RECT *rect)
{
	UINT64 work;

	if (!dirty || !rect || rect->left >= rect->right || rect->top >= rect->bottom) return;
	work = (UINT64)(UINT32)(rect->right - rect->left) * (UINT32)(rect->bottom - rect->top);
	dirty->workPixels += work;
	if (!dirty->valid) {
		dirty->valid = 1;
		dirty->left = rect->left;
		dirty->top = rect->top;
		dirty->right = rect->right;
		dirty->bottom = rect->bottom;
	}
	else {
		if (rect->left < dirty->left) dirty->left = rect->left;
		if (rect->top < dirty->top) dirty->top = rect->top;
		if (rect->right > dirty->right) dirty->right = rect->right;
		if (rect->bottom > dirty->bottom) dirty->bottom = rect->bottom;
	}
}

static UINT32 npdisp_w2k_stretchUnsupported(UINT32 reason)
{
	npdisp_w2k_traceStretchReject(reason);
	return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
}

UINT32 npdisp_w2k_hostCallStretch(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request)
{
	NPDISP_W2K_GUEST_RECTL guestDst;
	NPDISP_W2K_GUEST_RECTL guestSrc;
	NPDISP_W2K_RECT dstRect;
	NPDISP_W2K_RECT srcRect;
	std::vector<NPDISP_W2K_RECT> clipRects;
	std::vector<UINT8> source;
	std::vector<UINT32> xMap;
	NPDISP_W2K_STRETCH_DIRTY dirty;
	UINT32 bytesPerPixel;
	UINT32 sourceStride = 0;
	UINT32 pixelMask;
	UINT32 pattern = 0;
	UINT8 rop3 = 0xcc;
	int usesSource = 1;
	int usesPattern = 0;
	int dstWidth;
	int dstHeight;
	int srcWidth = 0;
	int srcHeight = 0;
	size_t i;

	if (!request || !npdisp.w2kLfb || !npdisp.enabled || !npdispwin.pBits || npdispwin.stride == 0) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_STATE);
	if (!(request->surfaceFlags & NPDISP_W2K_GDI_DST_SCREEN)) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_SURFACE);
	if (request->arg[2] != 0 || (request->surfaceFlags & NPDISP_W2K_GDI_MASK_SCREEN)) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_MASK);
	npdisp_w2k_traceStretchMode(request->value[0]);
	if (request->value[0] != NPDISP_W2K_COLORONCOLOR) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_MODE);
	if (request->arg[6] == 0) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	bytesPerPixel = npdisp_w2k_stretchBytesPerPixel();
	if (bytesPerPixel == 0) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_FORMAT);
	if (request->operation == NPDISP_W2K_GDI_STRETCH_BLT_ROP) {
		if (!npdisp_w2k_stretchRopInfo(request->value[1], &rop3, &usesSource, &usesPattern)) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_ROP);
		if (usesPattern) {
			if (request->value[2] == 0xffffffffUL) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_PATTERN);
			pattern = request->value[2];
		}
	}
	else if (request->operation != NPDISP_W2K_GDI_STRETCH_BLT) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (usesSource && !(request->surfaceFlags & NPDISP_W2K_GDI_XLATE_TRIVIAL)) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_XLATE);
	npdisp_w2k_guestRead(request->arg[6], &guestDst, sizeof(guestDst));
	dstRect.left = guestDst.left + request->dstOriginX;
	dstRect.top = guestDst.top + request->dstOriginY;
	dstRect.right = guestDst.right + request->dstOriginX;
	dstRect.bottom = guestDst.bottom + request->dstOriginY;
	dstWidth = dstRect.right - dstRect.left;
	dstHeight = dstRect.bottom - dstRect.top;
	if (dstWidth <= 0 || dstHeight <= 0) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_RECT);
	if (!npdisp_w2k_stretchBuildClipRects(requestAddr, request, &dstRect, &clipRects)) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_CLIP);
	if (clipRects.empty()) return NPDISP_W2K_HOSTCALL_OK;
	if (usesSource) {
		if (request->arg[1] == 0 || request->arg[7] == 0) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_w2k_guestRead(request->arg[7], &guestSrc, sizeof(guestSrc));
		srcRect.left = guestSrc.left + ((request->surfaceFlags & NPDISP_W2K_GDI_SRC_SCREEN) ? request->srcOriginX : 0);
		srcRect.top = guestSrc.top + ((request->surfaceFlags & NPDISP_W2K_GDI_SRC_SCREEN) ? request->srcOriginY : 0);
		srcRect.right = guestSrc.right + ((request->surfaceFlags & NPDISP_W2K_GDI_SRC_SCREEN) ? request->srcOriginX : 0);
		srcRect.bottom = guestSrc.bottom + ((request->surfaceFlags & NPDISP_W2K_GDI_SRC_SCREEN) ? request->srcOriginY : 0);
		srcWidth = srcRect.right - srcRect.left;
		srcHeight = srcRect.bottom - srcRect.top;
		if (srcWidth <= 0 || srcHeight <= 0) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_RECT);
		if (!npdisp_w2k_stretchSnapshotSource(request, &srcRect, bytesPerPixel, &source, &sourceStride)) return npdisp_w2k_stretchUnsupported(NPDISP_W2K_STRETCH_REJECT_SOURCE);
		// Only visible scanout columns need a mapping.  The guest may request
		// a much wider destination rectangle than the actual screen.
		xMap.resize(npdisp.width);
		for (i = 0; i < (size_t)npdisp.width; ++i) {
			SINT64 dx = (SINT64)i - (SINT64)dstRect.left;
			if (dx >= 0 && dx < dstWidth)
				xMap[i] = (UINT32)(((UINT64)dx * (UINT32)srcWidth) / (UINT32)dstWidth);
		}
	}
	memset(&dirty, 0, sizeof(dirty));
	pixelMask = npdisp_w2k_stretchPixelMask();
	pattern &= pixelMask;
	for (i = 0; i < clipRects.size(); ++i) {
		const NPDISP_W2K_RECT *clip = &clipRects[i];
		int y;
		if (usesSource && rop3 == 0xcc && srcWidth == dstWidth && srcHeight == dstHeight) {
			UINT32 rowBytes = (UINT32)(clip->right - clip->left) * bytesPerPixel;
			for (y = clip->top; y < clip->bottom; ++y) {
				int sy = y - dstRect.top;
				int sx = clip->left - dstRect.left;
				UINT8 *dst = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride + (UINT32)clip->left * bytesPerPixel;
				const UINT8 *src = &source[(size_t)sy * sourceStride + (size_t)sx * bytesPerPixel];
				memcpy(dst, src, rowBytes);
			}
			npdisp_w2k_stretchMarkDirty(&dirty, clip);
			continue;
		}
		for (y = clip->top; y < clip->bottom; ++y) {
			UINT8 *dst = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride + (UINT32)clip->left * bytesPerPixel;
			UINT32 sy = 0;
			int x;
			if (usesSource) sy = (UINT32)(((UINT64)(UINT32)(y - dstRect.top) * (UINT32)srcHeight) / (UINT32)dstHeight);
			for (x = clip->left; x < clip->right; ++x) {
				UINT32 srcPixel = 0;
				UINT32 dstPixel;
				UINT32 result;
				if (usesSource) {
					UINT32 sx = xMap[x];
					srcPixel = npdisp_w2k_stretchReadPixel(&source[(size_t)sy * sourceStride + (size_t)sx * bytesPerPixel]);
				}
				if (rop3 == 0xcc) result = srcPixel;
				else if (rop3 == 0xf0) result = pattern;
				else {
					dstPixel = npdisp_w2k_stretchReadPixel(dst);
					result = npdisp_w2k_stretchEvalRop(rop3, dstPixel, srcPixel, pattern, pixelMask);
				}
				npdisp_w2k_stretchWritePixel(dst, result);
				dst += bytesPerPixel;
			}
		}
		if (rop3 != 0xaa) npdisp_w2k_stretchMarkDirty(&dirty, clip);
		else dirty.workPixels += (UINT64)(UINT32)(clip->right - clip->left) * (UINT32)(clip->bottom - clip->top);
	}
	if (dirty.valid) {
		npdisp_setDirty(dirty.left, dirty.top, dirty.right, dirty.bottom);
		npdisp.updated = 1;
	}
	npdisp_w2k_chargePixels(dirty.workPixels);
	return NPDISP_W2K_HOSTCALL_OK;
}

#endif
