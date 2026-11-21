/**
 * @file	npdisp_w2k_text.cpp
 * @brief	NPDISP Windows 2000 TextOut host implementation
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>
#include <algorithm>

#include "pccore.h"
#include "../wab.h"

#include "../npdispdef.h"
#include "../npdisp.h"
#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"

extern NPDISP_WINDOWS npdispwin;

#define NPDISP_W2K_TEXT_MAX_GLYPHS       16384UL
#define NPDISP_W2K_TEXT_MAX_GLYPH_WIDTH  4096
#define NPDISP_W2K_TEXT_MAX_GLYPH_HEIGHT 4096
#define NPDISP_W2K_TEXT_MAX_GLYPH_BYTES  0x01000000UL
#define NPDISP_W2K_TEXT_MAX_TOTAL_BYTES  0x04000000UL

#define NPDISP_W2K_R2_BLACK        1
#define NPDISP_W2K_R2_NOTMERGEPEN  2
#define NPDISP_W2K_R2_MASKNOTPEN   3
#define NPDISP_W2K_R2_NOTCOPYPEN   4
#define NPDISP_W2K_R2_MASKPENNOT   5
#define NPDISP_W2K_R2_NOT          6
#define NPDISP_W2K_R2_XORPEN       7
#define NPDISP_W2K_R2_NOTMASKPEN   8
#define NPDISP_W2K_R2_MASKPEN      9
#define NPDISP_W2K_R2_NOTXORPEN   10
#define NPDISP_W2K_R2_NOP         11
#define NPDISP_W2K_R2_MERGENOTPEN 12
#define NPDISP_W2K_R2_COPYPEN     13
#define NPDISP_W2K_R2_MERGEPENNOT 14
#define NPDISP_W2K_R2_MERGEPEN    15
#define NPDISP_W2K_R2_WHITE       16

typedef struct {
	UINT32 hg;
	UINT32 pgdf;
	SINT32 x;
	SINT32 y;
} NPDISP_W2K_GUEST_GLYPHPOS;

typedef struct {
	SINT32 originX;
	SINT32 originY;
	SINT32 width;
	SINT32 height;
} NPDISP_W2K_GUEST_GLYPHBITS;

typedef struct {
	SINT32 left;
	SINT32 top;
	SINT32 right;
	SINT32 bottom;
} NPDISP_W2K_GUEST_RECTL;

typedef struct {
	int baseX;
	int baseY;
	int width;
	int height;
	UINT32 rowBytes;
	std::vector<UINT8> bits;
} NPDISP_W2K_TEXT_GLYPH;

typedef struct {
	int valid;
	int left;
	int top;
	int right;
	int bottom;
	UINT64 workPixels;
} NPDISP_W2K_TEXT_DIRTY;

/* TextOut は高頻度なので一時バッファを host-call 間で再利用する。 */
static std::vector<NPDISP_W2K_RECT> npdisp_w2k_textClipScratch;
static std::vector<NPDISP_W2K_RECT> npdisp_w2k_textGuestClipScratch;
static std::vector<NPDISP_W2K_GUEST_GLYPHPOS> npdisp_w2k_textGlyphScratch;
static std::vector<NPDISP_W2K_TEXT_GLYPH> npdisp_w2k_textPreparedScratch;

static int npdisp_w2k_textIntersect(NPDISP_W2K_RECT *dst, const NPDISP_W2K_RECT *a, const NPDISP_W2K_RECT *b)
{
	if (!dst || !a || !b) return 0;
	dst->left = (a->left > b->left) ? a->left : b->left;
	dst->top = (a->top > b->top) ? a->top : b->top;
	dst->right = (a->right < b->right) ? a->right : b->right;
	dst->bottom = (a->bottom < b->bottom) ? a->bottom : b->bottom;
	return dst->left < dst->right && dst->top < dst->bottom;
}

static int npdisp_w2k_textBuildClipRects(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request, std::vector<NPDISP_W2K_RECT> *clipRects)
{
	NPDISP_W2K_RECT screenRect;
	NPDISP_W2K_RECT clipRect;
	NPDISP_W2K_RECT clipped;
	std::vector<NPDISP_W2K_RECT> &guestRects = npdisp_w2k_textGuestClipScratch;
	UINT64 clipBytes;
	UINT32 i;

	if (!request || !clipRects) return 0;
	clipRects->clear();
	screenRect.left = 0;
	screenRect.top = 0;
	screenRect.right = npdisp.width;
	screenRect.bottom = npdisp.height;
	if (request->clipType == NPDISP_W2K_CLIP_TRIVIAL) {
		clipRects->push_back(screenRect);
		return 1;
	}
	if (request->clipType == NPDISP_W2K_CLIP_RECT) {
		clipRect.left = request->clipBounds.left + request->dstOriginX;
		clipRect.top = request->clipBounds.top + request->dstOriginY;
		clipRect.right = request->clipBounds.right + request->dstOriginX;
		clipRect.bottom = request->clipBounds.bottom + request->dstOriginY;
		if (npdisp_w2k_textIntersect(&clipped, &clipRect, &screenRect)) clipRects->push_back(clipped);
		return 1;
	}
	if (request->clipType != NPDISP_W2K_CLIP_COMPLEX || request->clipRectCount > NPDISP_W2K_MAX_CLIP_RECTS) return 0;
	guestRects.clear();
	if (request->clipRectCount == 0) return 1;
	clipBytes = (UINT64)request->clipRectCount * sizeof(NPDISP_W2K_RECT);
	if (request->clipRectOffset < sizeof(*request) || request->clipRectOffset > request->size ||
		clipBytes > request->size - request->clipRectOffset) return 0;
	guestRects.resize(request->clipRectCount);
	if (!npdisp_w2k_guestRead(requestAddr + request->clipRectOffset, &guestRects[0], (UINT32)clipBytes)) return 0;
	for (i = 0; i < request->clipRectCount; ++i) {
		clipRect.left = guestRects[i].left + request->dstOriginX;
		clipRect.top = guestRects[i].top + request->dstOriginY;
		clipRect.right = guestRects[i].right + request->dstOriginX;
		clipRect.bottom = guestRects[i].bottom + request->dstOriginY;
		if (npdisp_w2k_textIntersect(&clipped, &clipRect, &screenRect)) clipRects->push_back(clipped);
	}
	return 1;
}

static UINT32 npdisp_w2k_textPixelMask(void)
{
	if (npdisp.bpp == 8) return 0x000000ffUL;
	if (npdisp.bpp == 16) return 0x0000ffffUL;
	if (npdisp.bpp == 24) return 0x00ffffffUL;
	return 0xffffffffUL;
}

static UINT32 npdisp_w2k_textReadPixel(int x, int y)
{
	UINT8 *row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
	if (npdisp.bpp == 8) return row[x];
	if (npdisp.bpp == 16) return ((UINT16 *)row)[x];
	if (npdisp.bpp == 24) {
		UINT8 *p = row + (UINT32)x * 3;
		return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16);
	}
	return ((UINT32 *)row)[x];
}

static void npdisp_w2k_textWritePixel(int x, int y, UINT32 value)
{
	UINT8 *row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
	if (npdisp.bpp == 8) row[x] = (UINT8)value;
	else if (npdisp.bpp == 16) ((UINT16 *)row)[x] = (UINT16)value;
	else if (npdisp.bpp == 24) {
		UINT8 *p = row + (UINT32)x * 3;
		p[0] = (UINT8)value;
		p[1] = (UINT8)(value >> 8);
		p[2] = (UINT8)(value >> 16);
	}
	else ((UINT32 *)row)[x] = value;
}

static UINT32 npdisp_w2k_textEvalRop2(UINT32 rop2, UINT32 dst, UINT32 pen, UINT32 mask)
{
	UINT32 result;

	switch (rop2) {
	case NPDISP_W2K_R2_BLACK: result = 0; break;
	case NPDISP_W2K_R2_NOTMERGEPEN: result = ~(pen | dst); break;
	case NPDISP_W2K_R2_MASKNOTPEN: result = (~pen) & dst; break;
	case NPDISP_W2K_R2_NOTCOPYPEN: result = ~pen; break;
	case NPDISP_W2K_R2_MASKPENNOT: result = pen & ~dst; break;
	case NPDISP_W2K_R2_NOT: result = ~dst; break;
	case NPDISP_W2K_R2_XORPEN: result = pen ^ dst; break;
	case NPDISP_W2K_R2_NOTMASKPEN: result = ~(pen & dst); break;
	case NPDISP_W2K_R2_MASKPEN: result = pen & dst; break;
	case NPDISP_W2K_R2_NOTXORPEN: result = ~(pen ^ dst); break;
	case NPDISP_W2K_R2_NOP: result = dst; break;
	case NPDISP_W2K_R2_MERGENOTPEN: result = (~pen) | dst; break;
	case NPDISP_W2K_R2_COPYPEN: result = pen; break;
	case NPDISP_W2K_R2_MERGEPENNOT: result = pen | ~dst; break;
	case NPDISP_W2K_R2_MERGEPEN: result = pen | dst; break;
	case NPDISP_W2K_R2_WHITE: result = mask; break;
	default: result = dst; break;
	}
	return result & mask;
}

static int npdisp_w2k_textValidRop2(UINT32 rop2)
{
	return rop2 >= NPDISP_W2K_R2_BLACK && rop2 <= NPDISP_W2K_R2_WHITE;
}

static void npdisp_w2k_textMarkDirty(NPDISP_W2K_TEXT_DIRTY *dirty, int left, int top, int right, int bottom, UINT64 work)
{
	if (!dirty || left >= right || top >= bottom) return;
	if (!dirty->valid) {
		dirty->valid = 1;
		dirty->left = left;
		dirty->top = top;
		dirty->right = right;
		dirty->bottom = bottom;
	}
	else {
		if (left < dirty->left) dirty->left = left;
		if (top < dirty->top) dirty->top = top;
		if (right > dirty->right) dirty->right = right;
		if (bottom > dirty->bottom) dirty->bottom = bottom;
	}
	dirty->workPixels += work;
}

static int npdisp_w2k_textPointVisible(const std::vector<NPDISP_W2K_RECT> *clipRects, int x, int y)
{
	size_t i;

	if (!clipRects || x < 0 || y < 0 || x >= npdisp.width || y >= npdisp.height) return 0;
	for (i = 0; i < clipRects->size(); ++i) {
		const NPDISP_W2K_RECT *r = &(*clipRects)[i];
		if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) return 1;
	}
	return 0;
}

static void npdisp_w2k_textPlot(const std::vector<NPDISP_W2K_RECT> *clipRects, int x, int y, UINT32 color, UINT32 rop2, NPDISP_W2K_TEXT_DIRTY *dirty)
{
	UINT32 mask;
	UINT32 dst;
	UINT32 result;

	if (!npdisp_w2k_textPointVisible(clipRects, x, y)) return;
	if (dirty) dirty->workPixels++;
	if (rop2 == NPDISP_W2K_R2_NOP) return;
	mask = npdisp_w2k_textPixelMask();
	if (rop2 == NPDISP_W2K_R2_COPYPEN) result = color & mask;
	else {
		dst = npdisp_w2k_textReadPixel(x, y);
		result = npdisp_w2k_textEvalRop2(rop2, dst, color & mask, mask);
	}
	npdisp_w2k_textWritePixel(x, y, result);
	npdisp_w2k_textMarkDirty(dirty, x, y, x + 1, y + 1, 0);
}

static void npdisp_w2k_textFillRect(const std::vector<NPDISP_W2K_RECT> *clipRects, const NPDISP_W2K_RECT *rect, UINT32 color, NPDISP_W2K_TEXT_DIRTY *dirty)
{
	size_t i;
	UINT32 mask;

	if (!clipRects || !rect || !dirty) return;
	mask = npdisp_w2k_textPixelMask();
	color &= mask;
	for (i = 0; i < clipRects->size(); ++i) {
		NPDISP_W2K_RECT fill;
		int y;
		if (!npdisp_w2k_textIntersect(&fill, rect, &(*clipRects)[i])) continue;
		for (y = fill.top; y < fill.bottom; ++y) {
			UINT8 *row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
			int x;
			if (npdisp.bpp == 8) memset(row + fill.left, (UINT8)color, fill.right - fill.left);
			else if (npdisp.bpp == 16) {
				UINT16 *dst = (UINT16 *)row;
				std::fill(dst + fill.left, dst + fill.right, (UINT16)color);
			}
			else if (npdisp.bpp == 24) {
				UINT8 *dst = row + (UINT32)fill.left * 3;
				for (x = fill.left; x < fill.right; ++x) {
					dst[0] = (UINT8)color;
					dst[1] = (UINT8)(color >> 8);
					dst[2] = (UINT8)(color >> 16);
					dst += 3;
				}
			}
			else {
				UINT32 *dst = (UINT32 *)row;
				std::fill(dst + fill.left, dst + fill.right, color);
			}
		}
		npdisp_w2k_textMarkDirty(dirty, fill.left, fill.top, fill.right, fill.bottom,
			(UINT64)(UINT32)(fill.right - fill.left) * (UINT32)(fill.bottom - fill.top));
	}
}

static int npdisp_w2k_textPrepareGlyph(const NPDISP_W2K_GUEST_GLYPHPOS *glyph, SINT32 originX, SINT32 originY, NPDISP_W2K_TEXT_GLYPH *prepared, UINT64 *totalBytes)
{
	NPDISP_W2K_GUEST_GLYPHBITS header;
	UINT32 glyphBitsAddr;
	UINT64 glyphBytes;

	if (!glyph || !prepared || !totalBytes || glyph->pgdf == 0) return 0;
	if (!npdisp_w2k_guestRead(glyph->pgdf, &glyphBitsAddr, sizeof(glyphBitsAddr))) return 0;
	if (glyphBitsAddr == 0) return 0;
	if (!npdisp_w2k_guestRead(glyphBitsAddr, &header, sizeof(header))) return 0;
	if (header.width < 0 || header.height < 0 || header.width > NPDISP_W2K_TEXT_MAX_GLYPH_WIDTH ||
		header.height > NPDISP_W2K_TEXT_MAX_GLYPH_HEIGHT) return 0;
	prepared->baseX = glyph->x + header.originX + originX;
	prepared->baseY = glyph->y + header.originY + originY;
	prepared->width = header.width;
	prepared->height = header.height;
	prepared->rowBytes = ((UINT32)header.width + 7) >> 3;
	prepared->bits.clear();
	if (header.width == 0 || header.height == 0) return 1;
	glyphBytes = (UINT64)prepared->rowBytes * (UINT32)header.height;
	if (glyphBytes == 0 || glyphBytes > NPDISP_W2K_TEXT_MAX_GLYPH_BYTES ||
		*totalBytes > NPDISP_W2K_TEXT_MAX_TOTAL_BYTES - glyphBytes) return 0;
	if ((UINT64)glyphBitsAddr + sizeof(header) + glyphBytes > 0x100000000ULL) return 0;
	prepared->bits.resize((size_t)glyphBytes);
	if (!npdisp_w2k_guestRead(glyphBitsAddr + sizeof(header), &prepared->bits[0], (UINT32)glyphBytes)) return 0;
	*totalBytes += glyphBytes;
	return 1;
}

static int npdisp_w2k_textDrawGlyphCopyFast(const NPDISP_W2K_TEXT_GLYPH *glyph, const std::vector<NPDISP_W2K_RECT> *clipRects, UINT32 color, NPDISP_W2K_TEXT_DIRTY *dirty)
{
	const NPDISP_W2K_RECT *clip;
	int startX;
	int endX;
	int startY;
	int endY;
	int y;
	UINT64 pixels = 0;
	UINT32 mask;

	if (!glyph || !clipRects || clipRects->size() != 1 || !dirty || glyph->width <= 0 || glyph->height <= 0 || glyph->bits.empty()) return 0;
	clip = &(*clipRects)[0];
	startX = (glyph->baseX < clip->left) ? clip->left - glyph->baseX : 0;
	endX = (glyph->baseX + glyph->width > clip->right) ? clip->right - glyph->baseX : glyph->width;
	startY = (glyph->baseY < clip->top) ? clip->top - glyph->baseY : 0;
	endY = (glyph->baseY + glyph->height > clip->bottom) ? clip->bottom - glyph->baseY : glyph->height;
	if (startX < 0) startX = 0;
	if (startY < 0) startY = 0;
	if (endX > glyph->width) endX = glyph->width;
	if (endY > glyph->height) endY = glyph->height;
	if (startX >= endX || startY >= endY) return 1;
	mask = npdisp_w2k_textPixelMask();
	color &= mask;
	for (y = startY; y < endY; ++y) {
		const UINT8 *src = &glyph->bits[(size_t)y * glyph->rowBytes];
		int dstY = glyph->baseY + y;
		UINT8 *row = (UINT8 *)npdispwin.pBits + (UINT32)dstY * npdispwin.stride;
		int x;
		if (npdisp.bpp == 8) {
			UINT8 *dst = row + glyph->baseX;
			for (x = startX; x < endX;) {
				UINT8 bits = src[x >> 3];
				int byteEnd = ((x >> 3) + 1) << 3;
				if (byteEnd > endX) byteEnd = endX;
				if (bits == 0) { x = byteEnd; continue; }
				if (bits == 0xff && (x & 7) == 0 && byteEnd - x == 8) {
					memset(dst + x, (UINT8)color, 8);
					pixels += 8;
					x = byteEnd;
					continue;
				}
				for (; x < byteEnd; ++x) if (bits & (0x80 >> (x & 7))) { dst[x] = (UINT8)color; pixels++; }
			}
		}
		else if (npdisp.bpp == 16) {
			UINT16 *dst = (UINT16 *)row + glyph->baseX;
			for (x = startX; x < endX;) {
				UINT8 bits = src[x >> 3];
				int byteEnd = ((x >> 3) + 1) << 3;
				if (byteEnd > endX) byteEnd = endX;
				if (bits == 0) { x = byteEnd; continue; }
				if (bits == 0xff && (x & 7) == 0 && byteEnd - x == 8) {
					std::fill(dst + x, dst + x + 8, (UINT16)color);
					pixels += 8;
					x = byteEnd;
					continue;
				}
				for (; x < byteEnd; ++x) if (bits & (0x80 >> (x & 7))) { dst[x] = (UINT16)color; pixels++; }
			}
		}
		else if (npdisp.bpp == 24) {
			for (x = startX; x < endX;) {
				UINT8 bits = src[x >> 3];
				int byteEnd = ((x >> 3) + 1) << 3;
				if (byteEnd > endX) byteEnd = endX;
				if (bits == 0) { x = byteEnd; continue; }
				for (; x < byteEnd; ++x) {
					if (bits & (0x80 >> (x & 7))) {
						UINT8 *dst = row + (UINT32)(glyph->baseX + x) * 3;
						dst[0] = (UINT8)color;
						dst[1] = (UINT8)(color >> 8);
						dst[2] = (UINT8)(color >> 16);
						pixels++;
					}
				}
			}
		}
		else {
			UINT32 *dst = (UINT32 *)row + glyph->baseX;
			for (x = startX; x < endX;) {
				UINT8 bits = src[x >> 3];
				int byteEnd = ((x >> 3) + 1) << 3;
				if (byteEnd > endX) byteEnd = endX;
				if (bits == 0) { x = byteEnd; continue; }
				if (bits == 0xff && (x & 7) == 0 && byteEnd - x == 8) {
					std::fill(dst + x, dst + x + 8, color);
					pixels += 8;
					x = byteEnd;
					continue;
				}
				for (; x < byteEnd; ++x) if (bits & (0x80 >> (x & 7))) { dst[x] = color; pixels++; }
			}
		}
	}
	if (pixels != 0) npdisp_w2k_textMarkDirty(dirty, glyph->baseX + startX, glyph->baseY + startY, glyph->baseX + endX, glyph->baseY + endY, pixels);
	return 1;
}

static void npdisp_w2k_textDrawGlyph(const NPDISP_W2K_TEXT_GLYPH *glyph, const std::vector<NPDISP_W2K_RECT> *clipRects, UINT32 color, UINT32 rop2, NPDISP_W2K_TEXT_DIRTY *dirty)
{
	int y;

	if (!glyph || !clipRects || glyph->width <= 0 || glyph->height <= 0 || glyph->bits.empty()) return;
	if (rop2 == NPDISP_W2K_R2_COPYPEN && npdisp_w2k_textDrawGlyphCopyFast(glyph, clipRects, color, dirty)) return;
	for (y = 0; y < glyph->height; ++y) {
		const UINT8 *src = &glyph->bits[(size_t)y * glyph->rowBytes];
		int x;
		for (x = 0; x < glyph->width; ++x) {
			if (src[x >> 3] & (0x80 >> (x & 7))) npdisp_w2k_textPlot(clipRects, glyph->baseX + x, glyph->baseY + y, color, rop2, dirty);
		}
	}
}

UINT32 npdisp_w2k_hostCallText(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_GDI_REQUEST *request)
{
	std::vector<NPDISP_W2K_RECT> &clipRects = npdisp_w2k_textClipScratch;
	std::vector<NPDISP_W2K_GUEST_GLYPHPOS> &glyphs = npdisp_w2k_textGlyphScratch;
	std::vector<NPDISP_W2K_TEXT_GLYPH> &preparedGlyphs = npdisp_w2k_textPreparedScratch;
	NPDISP_W2K_TEXT_DIRTY dirty;
	NPDISP_W2K_GUEST_RECTL guestOpaque;
	NPDISP_W2K_RECT opaque;
	UINT64 glyphBytes;
	UINT32 glyphCount;
	UINT32 foregroundRop;
	UINT64 totalGlyphBytes = 0;
	UINT32 i;

	if (!request || !npdisp.w2kLfb || !npdisp.enabled || !npdispwin.pBits || npdispwin.stride == 0) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (npdisp.bpp != 8 && npdisp.bpp != 16 && npdisp.bpp != 24 && npdisp.bpp != 32) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (!(request->surfaceFlags & NPDISP_W2K_GDI_DST_SCREEN) || request->arg[3] != 0) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (request->value[2] == 0xffffffffUL) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	foregroundRop = request->value[0] & 0xff;
	if (!npdisp_w2k_textValidRop2(foregroundRop)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	glyphCount = request->value[1];
	if (glyphCount != request->extraCount || glyphCount > NPDISP_W2K_TEXT_MAX_GLYPHS || request->extraStride != sizeof(NPDISP_W2K_GUEST_GLYPHPOS)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	glyphBytes = (UINT64)glyphCount * request->extraStride;
	if (request->value[4] & NPDISP_W2K_GDI_TEXT_DIRECT_GLYPHS) {
		if (request->value[4] & ~NPDISP_W2K_GDI_TEXT_DIRECT_GLYPHS) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		if (glyphCount != 0 && request->arg[10] == 0) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		npdisp_w2k_traceTextDirect();
	}
	else {
		if (request->extraOffset < sizeof(*request) || request->extraOffset > request->size || glyphBytes > request->size - request->extraOffset) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	}
	if (!npdisp_w2k_textBuildClipRects(requestAddr, request, &clipRects)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (clipRects.empty()) return NPDISP_W2K_HOSTCALL_OK;
	if (request->arg[4] != 0) {
		if (request->value[3] == 0xffffffffUL) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		if (!npdisp_w2k_guestRead(request->arg[4], &guestOpaque, sizeof(guestOpaque))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		opaque.left = guestOpaque.left + request->dstOriginX;
		opaque.top = guestOpaque.top + request->dstOriginY;
		opaque.right = guestOpaque.right + request->dstOriginX;
		opaque.bottom = guestOpaque.bottom + request->dstOriginY;
	}
	if (glyphCount != 0) {
		if (glyphs.size() < glyphCount) glyphs.resize(glyphCount);
		if (preparedGlyphs.size() < glyphCount) preparedGlyphs.resize(glyphCount);
		UINT32 glyphAddr = (request->value[4] & NPDISP_W2K_GDI_TEXT_DIRECT_GLYPHS) ? request->arg[10] : requestAddr + request->extraOffset;
		if (!npdisp_w2k_guestRead(glyphAddr, &glyphs[0], (UINT32)glyphBytes)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
		for (i = 0; i < glyphCount; ++i) {
			if (!npdisp_w2k_textPrepareGlyph(&glyphs[i], request->dstOriginX, request->dstOriginY,
				&preparedGlyphs[i], &totalGlyphBytes)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		}
	}
	memset(&dirty, 0, sizeof(dirty));
	if (request->arg[4] != 0 && opaque.left < opaque.right && opaque.top < opaque.bottom) {
		npdisp_w2k_textFillRect(&clipRects, &opaque, request->value[3], &dirty);
	}
	for (i = 0; i < glyphCount; ++i) npdisp_w2k_textDrawGlyph(&preparedGlyphs[i], &clipRects, request->value[2], foregroundRop, &dirty);
	if (dirty.valid) {
		npdisp_setDirty(dirty.left, dirty.top, dirty.right, dirty.bottom);
		npdisp.updated = 1;
	}
	npdisp_w2k_chargePixels(dirty.workPixels);
	return NPDISP_W2K_HOSTCALL_OK;
}

#endif
