/**
 * @file	npdisp_w2k_gdi.cpp
 * @brief	NPDISP Windows 2000 generic GDI host-call dispatcher
 */

#include "compiler.h"

#if defined(SUPPORT_WAB_NPDISP) && defined(SUPPORT_NPDISP_W2K)

#include <map>
#include <vector>

#include "npdisp_w2k.h"
#include "npdisp_w2k_internal.h"

UINT32 npdisp_w2k_hostCallGdi(UINT32 requestAddr)
{
	NPDISP_W2K_HOSTCALL_GDI_REQUEST request;

	if (!npdisp_w2k_guestRead(requestAddr, &request, sizeof(request))) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.version != NPDISP_W2K_HOSTCALL_VERSION || request.size < sizeof(request) ||
		request.command != NPDISP_W2K_HOSTCALL_GDI) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;
	if (request.size > 0x01000000UL || (UINT64)requestAddr + request.size > 0x100000000ULL) return NPDISP_W2K_HOSTCALL_BAD_REQUEST;

	UINT32 status;

	switch (request.operation) {
	case NPDISP_W2K_GDI_TEXT_OUT:
		status = npdisp_w2k_hostCallText(requestAddr, &request);
		break;
	case NPDISP_W2K_GDI_STRETCH_BLT:
	case NPDISP_W2K_GDI_STRETCH_BLT_ROP:
		status = npdisp_w2k_hostCallStretch(requestAddr, &request);
		break;
	default:
		status = NPDISP_W2K_HOSTCALL_UNSUPPORTED;
		break;
	}
	npdisp_w2k_traceGdi(request.operation, status);
	return status;
}

#endif
