/**
 * @file	npdisp_w2k_pattern.cpp
 * @brief	NPDISP Windows 2000 realized pattern brush helper
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>

#include "pccore.h"

#include "../npdispdef.h"
#include "../npdisp.h"
#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"
#include "npdisp_w2k_pattern.h"

typedef struct {
	UINT32 iSolidColor;
	UINT32 pvRbrush;
	UINT32 flColorType;
} NPDISP_W2K_GUEST_BRUSHOBJ;

#define NPDISP_W2K_BMF_1BPP  1
#define NPDISP_W2K_BMF_4BPP  2
#define NPDISP_W2K_BMF_8BPP  3
#define NPDISP_W2K_BMF_16BPP 4
#define NPDISP_W2K_BMF_24BPP 5
#define NPDISP_W2K_BMF_32BPP 6
#define NPDISP_W2K_PATTERN_MAX_SIZE (16U * 1024U * 1024U)

static UINT32 npdisp_w2k_patternTargetFormat(void)
{
	if (npdisp.bpp == 8) return NPDISP_W2K_BMF_8BPP;
	if (npdisp.bpp == 16) return NPDISP_W2K_BMF_16BPP;
	if (npdisp.bpp == 24) return NPDISP_W2K_BMF_24BPP;
	if (npdisp.bpp == 32) return NPDISP_W2K_BMF_32BPP;
	return 0;
}

static UINT32 npdisp_w2k_patternPixelMask(void)
{
	if (npdisp.bpp == 8) return 0x000000ffUL;
	if (npdisp.bpp == 16) return 0x0000ffffUL;
	if (npdisp.bpp == 24) return 0x00ffffffUL;
	return 0xffffffffUL;
}

static UINT32 npdisp_w2k_patternMinStride(UINT32 width, UINT32 format)
{
	if (format == NPDISP_W2K_BMF_1BPP) return (width + 7) >> 3;
	if (format == NPDISP_W2K_BMF_4BPP) return (width + 1) >> 1;
	if (format == NPDISP_W2K_BMF_8BPP) return width;
	if (format == NPDISP_W2K_BMF_16BPP) return width * 2;
	if (format == NPDISP_W2K_BMF_24BPP) return width * 3;
	if (format == NPDISP_W2K_BMF_32BPP) return width * 4;
	return 0;
}

static int npdisp_w2k_patternRange(UINT32 offset, UINT32 size, UINT32 total)
{
	return offset <= total && size <= total - offset;
}

int npdisp_w2k_patternLoad(UINT32 brushObjAddr, NPDISP_W2K_PATTERN *pattern)
{
	NPDISP_W2K_GUEST_BRUSHOBJ brush;
	NPDISP_W2K_RBRUSH header;
	UINT32 targetFormat;
	UINT32 minStride;
	UINT32 xlateBytes;

	if (!brushObjAddr || !pattern) return 0;
	if (!npdisp_w2k_guestRead(brushObjAddr, &brush, sizeof(brush))) return 0;
	if (brush.iSolidColor != 0xffffffffUL || brush.pvRbrush == 0) return 0;
	if (!npdisp_w2k_guestRead(brush.pvRbrush, &header, sizeof(header))) return 0;
	targetFormat = npdisp_w2k_patternTargetFormat();
	if (header.signature != NPDISP_W2K_RBRUSH_SIGNATURE || header.size < sizeof(header) || header.size > NPDISP_W2K_PATTERN_MAX_SIZE ||
		header.width == 0 || header.height == 0 || header.targetFormat != targetFormat || header.patternStride <= 0 || header.maskSize != 0) return 0;
	minStride = npdisp_w2k_patternMinStride(header.width, header.patternFormat);
	if (minStride == 0 || (UINT32)header.patternStride < minStride || header.height > header.patternSize / (UINT32)header.patternStride ||
		!npdisp_w2k_patternRange(header.patternOffset, header.patternSize, header.size)) return 0;
	if (header.xlateCount != 0) {
		if (header.xlateCount > 256 || header.xlateCount > 0xffffffffUL / 4) return 0;
		xlateBytes = header.xlateCount * 4;
		if (!npdisp_w2k_patternRange(header.xlateOffset, xlateBytes, header.size)) return 0;
		if ((header.patternFormat == NPDISP_W2K_BMF_1BPP && header.xlateCount < 2) ||
			(header.patternFormat == NPDISP_W2K_BMF_4BPP && header.xlateCount < 16) ||
			(header.patternFormat == NPDISP_W2K_BMF_8BPP && header.xlateCount < 256)) return 0;
		if (header.patternFormat != NPDISP_W2K_BMF_1BPP && header.patternFormat != NPDISP_W2K_BMF_4BPP && header.patternFormat != NPDISP_W2K_BMF_8BPP) return 0;
	}
	else if (header.patternFormat != targetFormat) return 0;

	pattern->storage.resize(header.size);
	if (!npdisp_w2k_guestRead(brush.pvRbrush, &pattern->storage[0], header.size)) return 0;
	pattern->width = header.width;
	pattern->height = header.height;
	pattern->format = header.patternFormat;
	pattern->stride = (UINT32)header.patternStride;
	pattern->patternOffset = header.patternOffset;
	pattern->xlateOffset = header.xlateOffset;
	pattern->xlateCount = header.xlateCount;
	pattern->pixelMask = npdisp_w2k_patternPixelMask();
	return 1;
}

static int npdisp_w2k_patternMod(int value, UINT32 size)
{
	int result;
	if (size == 0) return 0;
	result = value % (int)size;
	if (result < 0) result += (int)size;
	return result;
}

UINT32 npdisp_w2k_patternPixel(const NPDISP_W2K_PATTERN *pattern, int x, int y, int originX, int originY)
{
	const UINT8 *row;
	UINT32 value;
	int px;
	int py;

	if (!pattern || pattern->storage.empty() || pattern->width == 0 || pattern->height == 0) return 0;
	px = npdisp_w2k_patternMod(x - originX, pattern->width);
	py = npdisp_w2k_patternMod(y - originY, pattern->height);
	row = &pattern->storage[pattern->patternOffset + (UINT32)py * pattern->stride];
	if (pattern->format == NPDISP_W2K_BMF_1BPP) value = (row[(UINT32)px >> 3] >> (7 - (px & 7))) & 1;
	else if (pattern->format == NPDISP_W2K_BMF_4BPP) value = ((px & 1) ? row[(UINT32)px >> 1] : (row[(UINT32)px >> 1] >> 4)) & 0x0f;
	else if (pattern->format == NPDISP_W2K_BMF_8BPP) value = row[px];
	else if (pattern->format == NPDISP_W2K_BMF_16BPP) value = (UINT32)row[(UINT32)px * 2] | ((UINT32)row[(UINT32)px * 2 + 1] << 8);
	else if (pattern->format == NPDISP_W2K_BMF_24BPP) value = (UINT32)row[(UINT32)px * 3] | ((UINT32)row[(UINT32)px * 3 + 1] << 8) | ((UINT32)row[(UINT32)px * 3 + 2] << 16);
	else value = (UINT32)row[(UINT32)px * 4] | ((UINT32)row[(UINT32)px * 4 + 1] << 8) | ((UINT32)row[(UINT32)px * 4 + 2] << 16) | ((UINT32)row[(UINT32)px * 4 + 3] << 24);
	if (pattern->xlateCount != 0) {
		const UINT8 *p = &pattern->storage[pattern->xlateOffset + value * 4];
		value = (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
	}
	return value & pattern->pixelMask;
}

#endif
