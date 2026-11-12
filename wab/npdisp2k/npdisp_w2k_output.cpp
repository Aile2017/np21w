/**
 * @file	npdisp_w2k_output.cpp
 * @brief	NPDISP Windows 2000 path output host implementation
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>
#include <algorithm>
#include <math.h>

#include "pccore.h"
#include "../wab.h"

#include "../npdispdef.h"
#include "../npdisp.h"
#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"
#include "npdisp_w2k_pattern.h"

extern NPDISP_WINDOWS npdispwin;

#define NPDISP_W2K_PD_BEGINSUBPATH 0x00000001UL
#define NPDISP_W2K_PD_ENDSUBPATH   0x00000002UL
#define NPDISP_W2K_PD_RESETSTYLE   0x00000004UL
#define NPDISP_W2K_PD_CLOSEFIGURE  0x00000008UL
#define NPDISP_W2K_PD_BEZIERS      0x00000010UL

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
	double x;
	double y;
} NPDISP_W2K_OUTPUT_POINT;

typedef struct {
	std::vector<NPDISP_W2K_OUTPUT_POINT> points;
	int closed;
} NPDISP_W2K_OUTPUT_SUBPATH;

typedef struct {
	double x;
	int winding;
} NPDISP_W2K_OUTPUT_CROSSING;

typedef struct {
	int valid;
	SINT32 left;
	SINT32 top;
	SINT32 right;
	SINT32 bottom;
	UINT64 workPixels;
} NPDISP_W2K_OUTPUT_DIRTY;

typedef struct {
	int patterned;
	UINT32 color;
	int originX;
	int originY;
	NPDISP_W2K_PATTERN pattern;
} NPDISP_W2K_OUTPUT_BRUSH;

/* Host-call は同期実行されるため、描画用一時領域を再利用する。 */
static std::vector<NPDISP_W2K_RECT> npdisp_w2k_outputClipScratch;
static std::vector<NPDISP_W2K_RECT> npdisp_w2k_outputGuestClipScratch;
static std::vector<NPDISP_W2K_OUTPUT_SUBPATH> npdisp_w2k_outputPathScratch;

static int npdisp_w2k_outputIntersect(NPDISP_W2K_RECT *dst, const NPDISP_W2K_RECT *a, const NPDISP_W2K_RECT *b)
{
	if (!dst || !a || !b) return 0;
	dst->left = (a->left > b->left) ? a->left : b->left;
	dst->top = (a->top > b->top) ? a->top : b->top;
	dst->right = (a->right < b->right) ? a->right : b->right;
	dst->bottom = (a->bottom < b->bottom) ? a->bottom : b->bottom;
	return dst->left < dst->right && dst->top < dst->bottom;
}

static int npdisp_w2k_outputBuildClipRects(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_OUTPUT_REQUEST *request, std::vector<NPDISP_W2K_RECT> *clipRects)
{
	NPDISP_W2K_RECT screenRect;
	NPDISP_W2K_RECT clipRect;
	NPDISP_W2K_RECT clipped;
	std::vector<NPDISP_W2K_RECT> &guestRects = npdisp_w2k_outputGuestClipScratch;
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
		if (npdisp_w2k_outputIntersect(&clipped, &clipRect, &screenRect)) clipRects->push_back(clipped);
		return 1;
	}
	if (request->clipType != NPDISP_W2K_CLIP_COMPLEX || request->clipRectCount > NPDISP_W2K_MAX_CLIP_RECTS) return 0;
	guestRects.clear();
	if (request->clipRectCount == 0) return 1;
	if (request->size < sizeof(*request) || request->clipRectCount > (request->size - sizeof(*request)) / sizeof(NPDISP_W2K_RECT)) return 0;
	guestRects.resize(request->clipRectCount);
	npdisp_w2k_guestRead(requestAddr + sizeof(*request), &guestRects[0], request->clipRectCount * sizeof(NPDISP_W2K_RECT));
	for (i = 0; i < request->clipRectCount; ++i) {
		clipRect.left = guestRects[i].left + request->dstOriginX;
		clipRect.top = guestRects[i].top + request->dstOriginY;
		clipRect.right = guestRects[i].right + request->dstOriginX;
		clipRect.bottom = guestRects[i].bottom + request->dstOriginY;
		if (npdisp_w2k_outputIntersect(&clipped, &clipRect, &screenRect)) clipRects->push_back(clipped);
	}
	return 1;
}

static UINT32 npdisp_w2k_outputPixelMask(void)
{
	if (npdisp.bpp == 8) return 0x000000ffUL;
	if (npdisp.bpp == 16) return 0x0000ffffUL;
	if (npdisp.bpp == 24) return 0x00ffffffUL;
	return 0xffffffffUL;
}

static UINT32 npdisp_w2k_outputEvalRop2(UINT32 mix, UINT32 dst, UINT32 pen, UINT32 mask)
{
	UINT32 result;

	switch (mix & 0xff) {
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

static int npdisp_w2k_outputValidMix(UINT32 mix)
{
	UINT32 rop2 = mix & 0xff;
	return rop2 >= NPDISP_W2K_R2_BLACK && rop2 <= NPDISP_W2K_R2_WHITE;
}

static void npdisp_w2k_outputMarkDirty(NPDISP_W2K_OUTPUT_DIRTY *dirty, int left, int top, int right, int bottom, UINT64 work)
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

static UINT32 npdisp_w2k_outputReadPixel(int x, int y)
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

static void npdisp_w2k_outputWritePixel(int x, int y, UINT32 value)
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

static int npdisp_w2k_outputPointVisible(const std::vector<NPDISP_W2K_RECT> *clipRects, int x, int y)
{
	size_t i;
	if (!clipRects || x < 0 || y < 0 || x >= npdisp.width || y >= npdisp.height) return 0;
	for (i = 0; i < clipRects->size(); ++i) {
		const NPDISP_W2K_RECT *r = &(*clipRects)[i];
		if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) return 1;
	}
	return 0;
}

static int npdisp_w2k_outputBrushInit(NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 brushAddr, UINT32 color, int originX, int originY)
{
	if (!brush) return 0;
	brush->patterned = 0;
	brush->color = color & npdisp_w2k_outputPixelMask();
	brush->originX = originX;
	brush->originY = originY;
	if (color != 0xffffffffUL) return 1;
	if (!brushAddr || !npdisp_w2k_patternLoad(brushAddr, &brush->pattern)) return 0;
	brush->patterned = 1;
	return 1;
}

static UINT32 npdisp_w2k_outputBrushPixel(const NPDISP_W2K_OUTPUT_BRUSH *brush, int x, int y)
{
	if (!brush) return 0;
	if (!brush->patterned) return brush->color;
	return npdisp_w2k_patternPixel(&brush->pattern, x, y, brush->originX, brush->originY);
}

static void npdisp_w2k_outputPlot(const std::vector<NPDISP_W2K_RECT> *clipRects, int x, int y, const NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 mix, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	UINT32 dst;
	UINT32 result;
	UINT32 mask;

	if (!npdisp_w2k_outputPointVisible(clipRects, x, y)) return;
	if ((mix & 0xff) == NPDISP_W2K_R2_NOP) {
		if (dirty) dirty->workPixels++;
		return;
	}
	mask = npdisp_w2k_outputPixelMask();
	dst = npdisp_w2k_outputReadPixel(x, y);
	result = npdisp_w2k_outputEvalRop2(mix, dst, npdisp_w2k_outputBrushPixel(brush, x, y) & mask, mask);
	npdisp_w2k_outputWritePixel(x, y, result);
	npdisp_w2k_outputMarkDirty(dirty, x, y, x + 1, y + 1, 1);
}

static void npdisp_w2k_outputSpanRaw(int y, int left, int right, const NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 mix, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	UINT32 rop2 = mix & 0xff;
	UINT32 mask = npdisp_w2k_outputPixelMask();
	int x;

	if (left >= right || y < 0 || y >= npdisp.height) return;
	if (left < 0) left = 0;
	if (right > npdisp.width) right = npdisp.width;
	if (left >= right) return;
	if (rop2 == NPDISP_W2K_R2_NOP) {
		if (dirty) dirty->workPixels += (UINT64)(right - left);
		return;
	}
	if (rop2 == NPDISP_W2K_R2_COPYPEN && brush && !brush->patterned) {
		UINT8 *row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
		if (npdisp.bpp == 8) memset(row + left, (UINT8)brush->color, right - left);
		else if (npdisp.bpp == 16) {
			UINT16 *dst = (UINT16 *)row;
			std::fill(dst + left, dst + right, (UINT16)brush->color);
		}
		else if (npdisp.bpp == 24) {
			UINT8 *dst = row + (UINT32)left * 3;
			for (x = left; x < right; ++x) {
				dst[0] = (UINT8)brush->color;
				dst[1] = (UINT8)(brush->color >> 8);
				dst[2] = (UINT8)(brush->color >> 16);
				dst += 3;
			}
		}
		else {
			UINT32 *dst = (UINT32 *)row;
			std::fill(dst + left, dst + right, brush->color);
		}
		npdisp_w2k_outputMarkDirty(dirty, left, y, right, y + 1, (UINT64)(right - left));
		return;
	}
	if (rop2 == NPDISP_W2K_R2_BLACK || rop2 == NPDISP_W2K_R2_WHITE) {
		UINT32 value = (rop2 == NPDISP_W2K_R2_BLACK) ? 0 : mask;
		UINT8 *row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
		if (npdisp.bpp == 8) memset(row + left, (UINT8)value, right - left);
		else if (npdisp.bpp == 16) {
			UINT16 *dst = (UINT16 *)row;
			std::fill(dst + left, dst + right, (UINT16)value);
		}
		else if (npdisp.bpp == 24) {
			memset(row + (UINT32)left * 3, (UINT8)value, (UINT32)(right - left) * 3);
		}
		else {
			UINT32 *dst = (UINT32 *)row;
			std::fill(dst + left, dst + right, value);
		}
		npdisp_w2k_outputMarkDirty(dirty, left, y, right, y + 1, (UINT64)(right - left));
		return;
	}
	for (x = left; x < right; ++x) {
		UINT32 dst = npdisp_w2k_outputReadPixel(x, y);
		UINT32 result = npdisp_w2k_outputEvalRop2(mix, dst, npdisp_w2k_outputBrushPixel(brush, x, y) & mask, mask);
		npdisp_w2k_outputWritePixel(x, y, result);
	}
	npdisp_w2k_outputMarkDirty(dirty, left, y, right, y + 1, (UINT64)(right - left));
}

static void npdisp_w2k_outputSpan(const std::vector<NPDISP_W2K_RECT> *clipRects, int y, int left, int right, const NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 mix, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	size_t i;
	if (!clipRects || left >= right) return;
	for (i = 0; i < clipRects->size(); ++i) {
		const NPDISP_W2K_RECT *r = &(*clipRects)[i];
		int x0;
		int x1;
		if (y < r->top || y >= r->bottom) continue;
		x0 = (left > r->left) ? left : r->left;
		x1 = (right < r->right) ? right : r->right;
		if (x0 < x1) npdisp_w2k_outputSpanRaw(y, x0, x1, brush, mix, dirty);
	}
}

static void npdisp_w2k_outputLine(const std::vector<NPDISP_W2K_RECT> *clipRects, int x0, int y0, int x1, int y1, const NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 mix, int includeLast, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
	int sx = (x0 < x1) ? 1 : -1;
	int dy = (y1 > y0) ? y0 - y1 : y1 - y0;
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		if (x0 == x1 && y0 == y1 && !includeLast) break;
		npdisp_w2k_outputPlot(clipRects, x0, y0, brush, mix, dirty);
		if (x0 == x1 && y0 == y1) break;
		{
			int e2 = err << 1;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}
}

static NPDISP_W2K_OUTPUT_POINT npdisp_w2k_outputPointFixed(const NPDISP_W2K_POINTFIX *point, SINT32 originX, SINT32 originY)
{
	NPDISP_W2K_OUTPUT_POINT result;
	result.x = (double)point->x / 16.0 + originX;
	result.y = (double)point->y / 16.0 + originY;
	return result;
}

static void npdisp_w2k_outputAppendBezier(std::vector<NPDISP_W2K_OUTPUT_POINT> *points, const NPDISP_W2K_OUTPUT_POINT *p1, const NPDISP_W2K_OUTPUT_POINT *p2, const NPDISP_W2K_OUTPUT_POINT *p3)
{
	NPDISP_W2K_OUTPUT_POINT p0;
	int steps;
	int i;
	double dx;
	double dy;
	double extent;

	if (!points || points->empty() || !p1 || !p2 || !p3) return;
	p0 = points->back();
	dx = p3->x - p0.x;
	dy = p3->y - p0.y;
	extent = fabs(dx) + fabs(dy) + fabs(p1->x - p0.x) + fabs(p1->y - p0.y) + fabs(p2->x - p3->x) + fabs(p2->y - p3->y);
	steps = (int)(extent / 12.0) + 8;
	if (steps < 8) steps = 8;
	if (steps > 64) steps = 64;
	for (i = 1; i <= steps; ++i) {
		double t = (double)i / steps;
		double u = 1.0 - t;
		NPDISP_W2K_OUTPUT_POINT p;
		p.x = u * u * u * p0.x + 3.0 * u * u * t * p1->x + 3.0 * u * t * t * p2->x + t * t * t * p3->x;
		p.y = u * u * u * p0.y + 3.0 * u * u * t * p1->y + 3.0 * u * t * t * p2->y + t * t * t * p3->y;
		points->push_back(p);
	}
}

static int npdisp_w2k_outputReadPath(UINT32 requestAddr, const NPDISP_W2K_HOSTCALL_OUTPUT_REQUEST *request, std::vector<NPDISP_W2K_OUTPUT_SUBPATH> *paths)
{
	std::vector<NPDISP_W2K_PATH_RECORD> records;
	std::vector<NPDISP_W2K_POINTFIX> points;
	UINT32 payloadOffset;
	UINT32 recordsOffset;
	UINT32 pointsOffset;
	UINT32 pointIndex = 0;
	UINT32 i;
	NPDISP_W2K_OUTPUT_SUBPATH *current = NULL;

	if (!request || !paths) return 0;
	paths->clear();
	if (request->pathRecordCount > NPDISP_W2K_MAX_PATH_RECORDS || request->pathPointCount > NPDISP_W2K_MAX_PATH_POINTS) return 0;
	payloadOffset = sizeof(*request);
	if (request->clipRectCount > (0xffffffffUL - payloadOffset) / sizeof(NPDISP_W2K_RECT)) return 0;
	recordsOffset = payloadOffset + request->clipRectCount * sizeof(NPDISP_W2K_RECT);
	if (request->pathRecordCount > (0xffffffffUL - recordsOffset) / sizeof(NPDISP_W2K_PATH_RECORD)) return 0;
	pointsOffset = recordsOffset + request->pathRecordCount * sizeof(NPDISP_W2K_PATH_RECORD);
	if (request->pathPointCount > (0xffffffffUL - pointsOffset) / sizeof(NPDISP_W2K_POINTFIX)) return 0;
	if (pointsOffset + request->pathPointCount * sizeof(NPDISP_W2K_POINTFIX) > request->size) return 0;
	if (request->pathRecordCount) {
		records.resize(request->pathRecordCount);
		npdisp_w2k_guestRead(requestAddr + recordsOffset, &records[0], request->pathRecordCount * sizeof(NPDISP_W2K_PATH_RECORD));
	}
	if (request->pathPointCount) {
		points.resize(request->pathPointCount);
		npdisp_w2k_guestRead(requestAddr + pointsOffset, &points[0], request->pathPointCount * sizeof(NPDISP_W2K_POINTFIX));
	}
	for (i = 0; i < request->pathRecordCount; ++i) {
		const NPDISP_W2K_PATH_RECORD *record = &records[i];
		UINT32 j = 0;
		if (record->pointCount > request->pathPointCount - pointIndex) return 0;
		if (record->flags & NPDISP_W2K_PD_BEGINSUBPATH) {
			NPDISP_W2K_OUTPUT_SUBPATH subpath;
			if (record->pointCount == 0) return 0;
			subpath.closed = 0;
			paths->push_back(subpath);
			current = &paths->back();
			current->points.push_back(npdisp_w2k_outputPointFixed(&points[pointIndex], request->dstOriginX, request->dstOriginY));
			j = 1;
		}
		if (!current) return 0;
		if (record->flags & NPDISP_W2K_PD_BEZIERS) {
			while (j + 2 < record->pointCount) {
				NPDISP_W2K_OUTPUT_POINT p1 = npdisp_w2k_outputPointFixed(&points[pointIndex + j], request->dstOriginX, request->dstOriginY);
				NPDISP_W2K_OUTPUT_POINT p2 = npdisp_w2k_outputPointFixed(&points[pointIndex + j + 1], request->dstOriginX, request->dstOriginY);
				NPDISP_W2K_OUTPUT_POINT p3 = npdisp_w2k_outputPointFixed(&points[pointIndex + j + 2], request->dstOriginX, request->dstOriginY);
				npdisp_w2k_outputAppendBezier(&current->points, &p1, &p2, &p3);
				j += 3;
			}
			if (j != record->pointCount) return 0;
		}
		else {
			for (; j < record->pointCount; ++j) current->points.push_back(npdisp_w2k_outputPointFixed(&points[pointIndex + j], request->dstOriginX, request->dstOriginY));
		}
		if (record->flags & NPDISP_W2K_PD_CLOSEFIGURE) current->closed = 1;
		pointIndex += record->pointCount;
		if (record->flags & NPDISP_W2K_PD_ENDSUBPATH) current = NULL;
	}
	return pointIndex == request->pathPointCount;
}

static int npdisp_w2k_outputCrossingLess(const NPDISP_W2K_OUTPUT_CROSSING &a, const NPDISP_W2K_OUTPUT_CROSSING &b)
{
	return a.x < b.x;
}

static void npdisp_w2k_outputFillPaths(const std::vector<NPDISP_W2K_OUTPUT_SUBPATH> *paths, const std::vector<NPDISP_W2K_RECT> *clipRects, const NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 mix, int winding, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	double minY = 0.0;
	double maxY = 0.0;
	int haveBounds = 0;
	int y;
	size_t p;
	std::vector<NPDISP_W2K_OUTPUT_CROSSING> crossings;

	if (!paths || !clipRects) return;
	for (p = 0; p < paths->size(); ++p) {
		size_t i;
		for (i = 0; i < (*paths)[p].points.size(); ++i) {
			double py = (*paths)[p].points[i].y;
			if (!haveBounds) { minY = maxY = py; haveBounds = 1; }
			else { if (py < minY) minY = py; if (py > maxY) maxY = py; }
		}
	}
	if (!haveBounds) return;
	for (y = (int)floor(minY); y < (int)ceil(maxY); ++y) {
		double scanY = y + 0.5;
		crossings.clear();
		for (p = 0; p < paths->size(); ++p) {
			const std::vector<NPDISP_W2K_OUTPUT_POINT> *points = &(*paths)[p].points;
			size_t count = points->size();
			size_t i;
			if (count < 2) continue;
			for (i = 0; i < count; ++i) {
				const NPDISP_W2K_OUTPUT_POINT *a = &(*points)[i];
				const NPDISP_W2K_OUTPUT_POINT *b = &(*points)[(i + 1) % count];
				NPDISP_W2K_OUTPUT_CROSSING crossing;
				if (a->y == b->y) continue;
				if (!((a->y <= scanY && scanY < b->y) || (b->y <= scanY && scanY < a->y))) continue;
				crossing.x = a->x + (scanY - a->y) * (b->x - a->x) / (b->y - a->y);
				crossing.winding = (b->y > a->y) ? 1 : -1;
				crossings.push_back(crossing);
			}
		}
		if (crossings.size() < 2) continue;
		std::sort(crossings.begin(), crossings.end(), npdisp_w2k_outputCrossingLess);
		if (!winding) {
			size_t i;
			for (i = 0; i + 1 < crossings.size(); i += 2) {
				int left = (int)ceil(crossings[i].x - 0.5);
				int right = (int)ceil(crossings[i + 1].x - 0.5);
				npdisp_w2k_outputSpan(clipRects, y, left, right, brush, mix, dirty);
			}
		}
		else {
			int level = 0;
			double startX = 0.0;
			size_t i;
			for (i = 0; i < crossings.size(); ++i) {
				int oldLevel = level;
				level += crossings[i].winding;
				if (oldLevel == 0 && level != 0) startX = crossings[i].x;
				else if (oldLevel != 0 && level == 0) {
					int left = (int)ceil(startX - 0.5);
					int right = (int)ceil(crossings[i].x - 0.5);
					npdisp_w2k_outputSpan(clipRects, y, left, right, brush, mix, dirty);
				}
			}
		}
	}
}

static int npdisp_w2k_outputDetectRect(const std::vector<NPDISP_W2K_OUTPUT_SUBPATH> *paths, double *left, double *top, double *right, double *bottom)
{
	const std::vector<NPDISP_W2K_OUTPUT_POINT> *points;
	size_t count;
	size_t i;
	UINT32 corners = 0;
	double minX;
	double minY;
	double maxX;
	double maxY;

	if (!paths || paths->size() != 1 || !left || !top || !right || !bottom) return 0;
	points = &(*paths)[0].points;
	count = points->size();
	if (count == 5 && (*points)[0].x == (*points)[4].x && (*points)[0].y == (*points)[4].y) count = 4;
	if (count != 4 || (!(*paths)[0].closed && points->size() != 5)) return 0;
	minX = maxX = (*points)[0].x;
	minY = maxY = (*points)[0].y;
	for (i = 1; i < count; ++i) {
		if ((*points)[i].x < minX) minX = (*points)[i].x;
		if ((*points)[i].x > maxX) maxX = (*points)[i].x;
		if ((*points)[i].y < minY) minY = (*points)[i].y;
		if ((*points)[i].y > maxY) maxY = (*points)[i].y;
	}
	if (minX >= maxX || minY >= maxY) return 0;
	for (i = 0; i < count; ++i) {
		const NPDISP_W2K_OUTPUT_POINT *a = &(*points)[i];
		const NPDISP_W2K_OUTPUT_POINT *b = &(*points)[(i + 1) % count];
		UINT32 corner;
		if (a->x != b->x && a->y != b->y) return 0;
		if (a->x == minX && a->y == minY) corner = 1;
		else if (a->x == maxX && a->y == minY) corner = 2;
		else if (a->x == maxX && a->y == maxY) corner = 4;
		else if (a->x == minX && a->y == maxY) corner = 8;
		else return 0;
		corners |= corner;
	}
	if (corners != 15) return 0;
	*left = minX;
	*top = minY;
	*right = maxX;
	*bottom = maxY;
	return 1;
}

static void npdisp_w2k_outputRect(const NPDISP_W2K_HOSTCALL_OUTPUT_REQUEST *request, const std::vector<NPDISP_W2K_RECT> *clipRects, const NPDISP_W2K_OUTPUT_BRUSH *strokeBrush, const NPDISP_W2K_OUTPUT_BRUSH *fillBrush, double left, double top, double right, double bottom, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	int fillLeft = (int)ceil(left - 0.5);
	int fillTop = (int)ceil(top - 0.5);
	int fillRight = (int)ceil(right - 0.5);
	int fillBottom = (int)ceil(bottom - 0.5);
	int x0 = (int)floor(left + 0.5);
	int y0 = (int)floor(top + 0.5);
	int x1 = (int)floor(right + 0.5);
	int y1 = (int)floor(bottom + 0.5);
	int y;

	if ((request->operation & NPDISP_W2K_OUTPUT_FILL) != 0) {
		for (y = fillTop; y < fillBottom; ++y) npdisp_w2k_outputSpan(clipRects, y, fillLeft, fillRight, fillBrush, request->fillMix, dirty);
	}
	if ((request->operation & NPDISP_W2K_OUTPUT_STROKE) != 0 && x0 != x1 && y0 != y1) {
		if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
		if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
		npdisp_w2k_outputSpan(clipRects, y0, x0, x1, strokeBrush, request->strokeMix, dirty);
		npdisp_w2k_outputSpan(clipRects, y1, x0 + 1, x1 + 1, strokeBrush, request->strokeMix, dirty);
		for (y = y0; y < y1; ++y) npdisp_w2k_outputPlot(clipRects, x1, y, strokeBrush, request->strokeMix, dirty);
		for (y = y0 + 1; y <= y1; ++y) npdisp_w2k_outputPlot(clipRects, x0, y, strokeBrush, request->strokeMix, dirty);
	}
}

static void npdisp_w2k_outputStrokePaths(const std::vector<NPDISP_W2K_OUTPUT_SUBPATH> *paths, const std::vector<NPDISP_W2K_RECT> *clipRects, const NPDISP_W2K_OUTPUT_BRUSH *brush, UINT32 mix, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	size_t p;
	if (!paths || !clipRects) return;
	for (p = 0; p < paths->size(); ++p) {
		const std::vector<NPDISP_W2K_OUTPUT_POINT> *points = &(*paths)[p].points;
		size_t i;
		if (points->size() < 2) continue;
		for (i = 1; i < points->size(); ++i) {
			int x0 = (int)floor((*points)[i - 1].x + 0.5);
			int y0 = (int)floor((*points)[i - 1].y + 0.5);
			int x1 = (int)floor((*points)[i].x + 0.5);
			int y1 = (int)floor((*points)[i].y + 0.5);
			npdisp_w2k_outputLine(clipRects, x0, y0, x1, y1, brush, mix, (i + 1 == points->size() && !(*paths)[p].closed), dirty);
		}
		if ((*paths)[p].closed) {
			int x0 = (int)floor(points->back().x + 0.5);
			int y0 = (int)floor(points->back().y + 0.5);
			int x1 = (int)floor((*points)[0].x + 0.5);
			int y1 = (int)floor((*points)[0].y + 0.5);
			npdisp_w2k_outputLine(clipRects, x0, y0, x1, y1, brush, mix, 0, dirty);
		}
	}
}

static UINT64 npdisp_w2k_outputSolidSpanFast(const NPDISP_W2K_RECT *clip, int y, int left, int right, UINT32 color)
{
	UINT8 *row;
	int x;

	if (!clip || y < clip->top || y >= clip->bottom || left >= right) return 0;
	if (left < clip->left) left = clip->left;
	if (right > clip->right) right = clip->right;
	if (left >= right) return 0;
	row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
	if (npdisp.bpp == 8) memset(row + left, (UINT8)color, right - left);
	else if (npdisp.bpp == 16) {
		UINT16 *dst = (UINT16 *)row;
		std::fill(dst + left, dst + right, (UINT16)color);
	}
	else if (npdisp.bpp == 24) {
		UINT8 *dst = row + (UINT32)left * 3;
		for (x = left; x < right; ++x) {
			dst[0] = (UINT8)color;
			dst[1] = (UINT8)(color >> 8);
			dst[2] = (UINT8)(color >> 16);
			dst += 3;
		}
	}
	else {
		UINT32 *dst = (UINT32 *)row;
		std::fill(dst + left, dst + right, color);
	}
	return (UINT64)(right - left);
}

static UINT64 npdisp_w2k_outputSolidPlotFast(const NPDISP_W2K_RECT *clip, int x, int y, UINT32 color)
{
	UINT8 *row;

	if (!clip || x < clip->left || x >= clip->right || y < clip->top || y >= clip->bottom) return 0;
	row = (UINT8 *)npdispwin.pBits + (UINT32)y * npdispwin.stride;
	if (npdisp.bpp == 8) row[x] = (UINT8)color;
	else if (npdisp.bpp == 16) ((UINT16 *)row)[x] = (UINT16)color;
	else if (npdisp.bpp == 24) {
		UINT8 *dst = row + (UINT32)x * 3;
		dst[0] = (UINT8)color;
		dst[1] = (UINT8)(color >> 8);
		dst[2] = (UINT8)(color >> 16);
	}
	else ((UINT32 *)row)[x] = color;
	return 1;
}

static void npdisp_w2k_outputEllipseStrokeFill(const std::vector<NPDISP_W2K_RECT> *clipRects, int x0, int y0, int x1, int y1, const NPDISP_W2K_OUTPUT_BRUSH *strokeBrush, UINT32 strokeMix, const NPDISP_W2K_OUTPUT_BRUSH *fillBrush, UINT32 fillMix, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	SINT64 a;
	SINT64 b;
	SINT64 b1;
	SINT64 dx;
	SINT64 dy;
	SINT64 err;
	SINT64 e2;
	int fillTop = 0x7fffffff;
	int fillBottom = 0x7fffffff;
	int boundLeft;
	int boundTop;
	int boundRight;
	int boundBottom;
	int fast;
	UINT64 fastWork = 0;
	const NPDISP_W2K_RECT *fastClip = NULL;
	UINT32 strokeColor = 0;
	UINT32 fillColor = 0;

	if (!clipRects || !dirty || (strokeBrush == NULL && fillBrush == NULL)) return;
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	boundLeft = x0;
	boundTop = y0;
	boundRight = x1 + 1;
	boundBottom = y1 + 1;
	fast = (clipRects->size() == 1 &&
		(strokeBrush == NULL || (((strokeMix & 0xff) == NPDISP_W2K_R2_COPYPEN) && !strokeBrush->patterned)) &&
		(fillBrush == NULL || (((fillMix & 0xff) == NPDISP_W2K_R2_COPYPEN) && !fillBrush->patterned)));
	if (fast) {
		fastClip = &(*clipRects)[0];
		if (strokeBrush) strokeColor = strokeBrush->color & npdisp_w2k_outputPixelMask();
		if (fillBrush) fillColor = fillBrush->color & npdisp_w2k_outputPixelMask();
	}
	a = x1 - x0;
	b = y1 - y0;
	b1 = b & 1;
	dx = 4 * (1 - a) * b * b;
	dy = 4 * (b1 + 1) * a * a;
	err = dx + dy + b1 * a * a;
	y0 += (int)((b + 1) / 2);
	y1 = y0 - (int)b1;
	a = 8 * a * a;
	b1 = 8 * b * b;
	do {
		if (fillBrush != NULL) {
			if (y0 != fillBottom) {
				if (fast) fastWork += npdisp_w2k_outputSolidSpanFast(fastClip, y0, x0, x1 + 1, fillColor);
				else npdisp_w2k_outputSpan(clipRects, y0, x0, x1 + 1, fillBrush, fillMix, dirty);
				fillBottom = y0;
			}
			if (y1 != y0 && y1 != fillTop) {
				if (fast) fastWork += npdisp_w2k_outputSolidSpanFast(fastClip, y1, x0, x1 + 1, fillColor);
				else npdisp_w2k_outputSpan(clipRects, y1, x0, x1 + 1, fillBrush, fillMix, dirty);
				fillTop = y1;
			}
		}
		if (strokeBrush != NULL) {
			if (fast) {
				fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x1, y0, strokeColor);
				if (x0 != x1) fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x0, y0, strokeColor);
				if (y0 != y1) {
					fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x0, y1, strokeColor);
					if (x0 != x1) fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x1, y1, strokeColor);
				}
			}
			else {
				npdisp_w2k_outputPlot(clipRects, x1, y0, strokeBrush, strokeMix, dirty);
				if (x0 != x1) npdisp_w2k_outputPlot(clipRects, x0, y0, strokeBrush, strokeMix, dirty);
				if (y0 != y1) {
					npdisp_w2k_outputPlot(clipRects, x0, y1, strokeBrush, strokeMix, dirty);
					if (x0 != x1) npdisp_w2k_outputPlot(clipRects, x1, y1, strokeBrush, strokeMix, dirty);
				}
			}
		}
		e2 = 2 * err;
		if (e2 <= dy) { y0++; y1--; err += dy += a; }
		if (e2 >= dx || 2 * err > dy) { x0++; x1--; err += dx += b1; }
	} while (x0 <= x1);
	while (y0 - y1 < b) {
		if (fillBrush != NULL) {
			if (y0 != fillBottom) {
				if (fast) fastWork += npdisp_w2k_outputSolidSpanFast(fastClip, y0, x0 - 1, x1 + 2, fillColor);
				else npdisp_w2k_outputSpan(clipRects, y0, x0 - 1, x1 + 2, fillBrush, fillMix, dirty);
				fillBottom = y0;
			}
			if (y1 != y0 && y1 != fillTop) {
				if (fast) fastWork += npdisp_w2k_outputSolidSpanFast(fastClip, y1, x0 - 1, x1 + 2, fillColor);
				else npdisp_w2k_outputSpan(clipRects, y1, x0 - 1, x1 + 2, fillBrush, fillMix, dirty);
				fillTop = y1;
			}
		}
		if (strokeBrush != NULL) {
			if (fast) {
				fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x0 - 1, y0, strokeColor);
				fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x1 + 1, y0++, strokeColor);
				fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x0 - 1, y1, strokeColor);
				fastWork += npdisp_w2k_outputSolidPlotFast(fastClip, x1 + 1, y1--, strokeColor);
			}
			else {
				npdisp_w2k_outputPlot(clipRects, x0 - 1, y0, strokeBrush, strokeMix, dirty);
				npdisp_w2k_outputPlot(clipRects, x1 + 1, y0++, strokeBrush, strokeMix, dirty);
				npdisp_w2k_outputPlot(clipRects, x0 - 1, y1, strokeBrush, strokeMix, dirty);
				npdisp_w2k_outputPlot(clipRects, x1 + 1, y1--, strokeBrush, strokeMix, dirty);
			}
		}
		else { y0++; y1--; }
	}
	if (fast && fastWork != 0) {
		NPDISP_W2K_RECT bounds;
		NPDISP_W2K_RECT clipped;
		bounds.left = boundLeft;
		bounds.top = boundTop;
		bounds.right = boundRight;
		bounds.bottom = boundBottom;
		if (npdisp_w2k_outputIntersect(&clipped, &bounds, fastClip)) {
			npdisp_w2k_outputMarkDirty(dirty, clipped.left, clipped.top, clipped.right, clipped.bottom, fastWork);
			npdisp_w2k_traceFastEllipse();
		}
	}
}

static void npdisp_w2k_outputEllipse(const NPDISP_W2K_HOSTCALL_OUTPUT_REQUEST *request, const std::vector<NPDISP_W2K_RECT> *clipRects, const NPDISP_W2K_OUTPUT_BRUSH *strokeBrush, const NPDISP_W2K_OUTPUT_BRUSH *fillBrush, NPDISP_W2K_OUTPUT_DIRTY *dirty)
{
	double left;
	double top;
	double right;
	double bottom;
	int x0;
	int y0;
	int x1;
	int y1;
	const NPDISP_W2K_OUTPUT_BRUSH *stroke = NULL;
	const NPDISP_W2K_OUTPUT_BRUSH *fill = NULL;

	if (!request || !clipRects) return;
	left = (double)request->pathBounds.left / 16.0 + request->dstOriginX;
	top = (double)request->pathBounds.top / 16.0 + request->dstOriginY;
	right = (double)request->pathBounds.right / 16.0 + request->dstOriginX;
	bottom = (double)request->pathBounds.bottom / 16.0 + request->dstOriginY;
	if (right <= left || bottom <= top) return;
	x0 = (int)floor(left + 0.5);
	y0 = (int)floor(top + 0.5);
	x1 = (int)floor(right + 0.5);
	y1 = (int)floor(bottom + 0.5);
	if (x0 == x1 || y0 == y1) return;
	if (request->operation & NPDISP_W2K_OUTPUT_STROKE) stroke = strokeBrush;
	if (request->operation & NPDISP_W2K_OUTPUT_FILL) fill = fillBrush;
	/* GDIが示した外接矩形から同じ整数楕円を使用する。 */
	npdisp_w2k_outputEllipseStrokeFill(clipRects, x0, y0, x1, y1, stroke, request->strokeMix, fill, request->fillMix, dirty);
}

UINT32 npdisp_w2k_hostCallOutput(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_OUTPUT_REQUEST request;
	std::vector<NPDISP_W2K_RECT> &clipRects = npdisp_w2k_outputClipScratch;
	std::vector<NPDISP_W2K_OUTPUT_SUBPATH> &paths = npdisp_w2k_outputPathScratch;
	NPDISP_W2K_OUTPUT_DIRTY dirty;
	NPDISP_W2K_OUTPUT_BRUSH strokeBrush;
	NPDISP_W2K_OUTPUT_BRUSH fillBrush;

	paths.clear();
	if (!npdisp.w2kLfb || !npdisp.enabled || !npdispwin.pBits || npdispwin.stride == 0) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (npdisp.bpp != 8 && npdisp.bpp != 16 && npdisp.bpp != 24 && npdisp.bpp != 32) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	npdisp_w2k_guestRead(requestAddr, &request, sizeof(request));
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size < sizeof(request) || request.command != NPDISP_W2K_HOSTCALL_OUTPUT ||
		!(request.surfaceFlags & NPDISP_W2K_OUTPUT_DST_SCREEN) || (request.operation & ~(NPDISP_W2K_OUTPUT_STROKE | NPDISP_W2K_OUTPUT_FILL)) != 0 || request.operation == 0) {
		return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	}
	if ((request.operation & NPDISP_W2K_OUTPUT_STROKE) != 0 && (request.strokeWidth != 1 || !npdisp_w2k_outputValidMix(request.strokeMix))) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if ((request.operation & NPDISP_W2K_OUTPUT_FILL) != 0 && !npdisp_w2k_outputValidMix(request.fillMix)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if ((request.operation & NPDISP_W2K_OUTPUT_STROKE) != 0 && !npdisp_w2k_outputBrushInit(&strokeBrush, request.strokeBrushAddr, request.strokeColor, request.brushOriginX + request.dstOriginX, request.brushOriginY + request.dstOriginY)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if ((request.operation & NPDISP_W2K_OUTPUT_FILL) != 0 && !npdisp_w2k_outputBrushInit(&fillBrush, request.fillBrushAddr, request.fillColor, request.brushOriginX + request.dstOriginX, request.brushOriginY + request.dstOriginY)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (!npdisp_w2k_outputBuildClipRects(requestAddr, &request, &clipRects)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
	if (clipRects.empty()) return NPDISP_W2K_HOSTCALL_OK;
	memset(&dirty, 0, sizeof(dirty));
	if ((request.pathFlags & NPDISP_W2K_OUTPUT_PATH_ELLIPSE) && (request.pathFlags & NPDISP_W2K_OUTPUT_PATH_RECT)) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.pathFlags & NPDISP_W2K_OUTPUT_PATH_ELLIPSE) {
		npdisp_w2k_outputEllipse(&request, &clipRects, &strokeBrush, &fillBrush, &dirty);
		npdisp_w2k_traceOutput(NPDISP_W2K_OUTPUT_TRACE_ELLIPSE, dirty.workPixels);
	}
	else if (request.pathFlags & NPDISP_W2K_OUTPUT_PATH_RECT) {
		npdisp_w2k_traceFastRect();
		double rectLeft = (double)request.pathBounds.left / 16.0 + request.dstOriginX;
		double rectTop = (double)request.pathBounds.top / 16.0 + request.dstOriginY;
		double rectRight = (double)request.pathBounds.right / 16.0 + request.dstOriginX;
		double rectBottom = (double)request.pathBounds.bottom / 16.0 + request.dstOriginY;
		npdisp_w2k_outputRect(&request, &clipRects, &strokeBrush, &fillBrush, rectLeft, rectTop, rectRight, rectBottom, &dirty);
		npdisp_w2k_traceOutput(NPDISP_W2K_OUTPUT_TRACE_RECT, dirty.workPixels);
	}
	else {
		double rectLeft;
		double rectTop;
		double rectRight;
		double rectBottom;
		if (!npdisp_w2k_outputReadPath(requestAddr, &request, &paths)) return NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		if (npdisp_w2k_outputDetectRect(&paths, &rectLeft, &rectTop, &rectRight, &rectBottom)) {
			npdisp_w2k_outputRect(&request, &clipRects, &strokeBrush, &fillBrush, rectLeft, rectTop, rectRight, rectBottom, &dirty);
			npdisp_w2k_traceOutput(NPDISP_W2K_OUTPUT_TRACE_RECT, dirty.workPixels);
		}
		else {
			if ((request.operation & NPDISP_W2K_OUTPUT_FILL) != 0) npdisp_w2k_outputFillPaths(&paths, &clipRects, &fillBrush, request.fillMix, (request.pathFlags & NPDISP_W2K_OUTPUT_FILL_WINDING) != 0, &dirty);
			if ((request.operation & NPDISP_W2K_OUTPUT_STROKE) != 0) npdisp_w2k_outputStrokePaths(&paths, &clipRects, &strokeBrush, request.strokeMix, &dirty);
			npdisp_w2k_traceOutput(NPDISP_W2K_OUTPUT_TRACE_GENERIC, dirty.workPixels);
		}
	}
	if (dirty.valid) {
		npdisp_setDirty(dirty.left, dirty.top, dirty.right, dirty.bottom);
		npdisp.updated = 1;
	}
	npdisp_w2k_chargePixels(dirty.workPixels);
	return NPDISP_W2K_HOSTCALL_OK;
}

#endif
