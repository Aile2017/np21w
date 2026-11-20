#ifndef CLIPMNG_H
#define CLIPMNG_H

#if defined(SUPPORT_NPCLIP)
void clipmng_initialize(HWND hWnd);
void clipmng_deinitialize(void);
BOOL clipmng_setmode(UINT8 mode);
#endif

#endif
