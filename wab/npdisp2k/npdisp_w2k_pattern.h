/**
 * @file	npdisp_w2k_pattern.h
 * @brief	NPDISP Windows 2000 realized pattern brush helper
 */

#pragma once

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <vector>

struct NPDISP_W2K_PATTERN {
	UINT32 width;
	UINT32 height;
	UINT32 format;
	UINT32 stride;
	UINT32 patternOffset;
	UINT32 xlateOffset;
	UINT32 xlateCount;
	UINT32 pixelMask;
	std::vector<UINT8> storage;

	NPDISP_W2K_PATTERN() : width(0), height(0), format(0), stride(0), patternOffset(0), xlateOffset(0), xlateCount(0), pixelMask(0) {}
};

int npdisp_w2k_patternLoad(UINT32 brushObjAddr, NPDISP_W2K_PATTERN *pattern);
UINT32 npdisp_w2k_patternPixel(const NPDISP_W2K_PATTERN *pattern, int x, int y, int originX, int originY);

#endif
