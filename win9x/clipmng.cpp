#include "compiler.h"
#include "clipmng.h"
#include "npclipio.h"

#if defined(SUPPORT_NPCLIP)
#include <stdlib.h>
#include <string.h>

static const TCHAR clipclass[] = TEXT("NP2Clipboard");
static HWND clipwnd;
static HWND nextviewer;
static BOOL viewer;
static BOOL listener;
static BOOL busy;
static BOOL again;
static BOOL retry;
static UINT8 clipmode;
static UINT32 clipsession;
static DWORD clipseq;
static DWORD viewseq;
static DWORD viewbase;
static BOOL (WINAPI *addlistener)(HWND);
static BOOL (WINAPI *removelistener)(HWND);
#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031d
#endif
static DWORD (WINAPI *getsequence)(void);

static DWORD sequence(void) {

	return getsequence ? getsequence() : viewseq;
}

static void baseline(void) {

	MSG msg;

	if (listener) {
		while (PeekMessage(&msg, clipwnd, WM_CLIPBOARDUPDATE, WM_CLIPBOARDUPDATE, PM_REMOVE)) {
		}
	}
	clipseq = sequence();
	viewbase = viewseq;
}

static BOOL changed(void) {

	return sequence() != clipseq || viewseq != viewbase;
}

// CF_LOCALEで指定されたANSIコードページを優先する。
static UINT ansicodepage(void) {

	HGLOBAL h;
	LCID *locale;
	char cp[16];
	UINT codepage = GetACP();

	h = (HGLOBAL)GetClipboardData(CF_LOCALE);
	if (h && GlobalSize(h) >= sizeof(LCID)) {
		locale = (LCID *)GlobalLock(h);
		if (locale) {
			if (GetLocaleInfoA(*locale, LOCALE_IDEFAULTANSICODEPAGE, cp, sizeof(cp))) {
				UINT value = (UINT)atoi(cp);
				if (value && IsValidCodePage(value)) {
					codepage = value;
				}
			}
			GlobalUnlock(h);
		}
	}
	return codepage;
}

// 終端NULを除いたUTF-16文字列を取得する。
// text=NULLは非テキストまたは取得不能。
static void readtext(WCHAR **text, UINT32 *length) {

	UINT format;
	UINT cp;
	HGLOBAL h;
	SIZE_T size;
	UINT32 len;
	const UINT8 *p;
	WCHAR *out = NULL;
	int count;

	*text = NULL;
	*length = 0;
	if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
		format = CF_UNICODETEXT;
		cp = 1200;
	}
	else if (IsClipboardFormatAvailable(CF_TEXT)) {
		format = CF_TEXT;
		cp = ansicodepage();
	}
	else if (IsClipboardFormatAvailable(CF_OEMTEXT)) {
		format = CF_OEMTEXT;
		cp = GetOEMCP();
	}
	else {
		return;
	}
	h = (HGLOBAL)GetClipboardData(format);
	if (!h) {
		return;
	}
	size = GlobalSize(h);
	p = (const UINT8 *)GlobalLock(h);
	if (!p) {
		return;
	}
	// GlobalSizeにはアロケータの余剰領域も含まれるため、NULまでを調べる。
	if (format == CF_UNICODETEXT) {
		const WCHAR *w = (const WCHAR *)p;
		for (len = 0; len <= NPCLIP_MAX_BYTES && len < size / sizeof(WCHAR); len++) {
			if (!w[len]) {
				break;
			}
		}
		if (len <= NPCLIP_MAX_BYTES && len < size / sizeof(WCHAR)) {
			out = (WCHAR *)malloc((len + 1) * sizeof(WCHAR));
			if (out) {
				memcpy(out, w, (len + 1) * sizeof(WCHAR));
			}
		}
	}
	else {
		for (len = 0; len <= NPCLIP_MAX_BYTES * 2 && len < size; len++) {
			if (!p[len]) {
				break;
			}
		}
		if (len <= NPCLIP_MAX_BYTES * 2 && len < size) {
			count = len ? MultiByteToWideChar(cp, 0, (LPCSTR)p, len, NULL, 0) : 0;
			if ((!len || count > 0) && count <= NPCLIP_MAX_BYTES) {
				out = (WCHAR *)malloc((count + 1) * sizeof(WCHAR));
				if (out) {
					if (count && MultiByteToWideChar(cp, 0, (LPCSTR)p, len, out, count) != count) {
						free(out);
						out = NULL;
					}
					else {
						out[count] = 0;
						len = count;
					}
				}
			}
		}
	}
	GlobalUnlock(h);
	if (out) {
		*text = out;
		*length = len;
	}
	return;
}

// ホストの更新をゲストのコードページへ変換する。
static BOOL publishtext(UINT32 session, UINT8 format, UINT16 cp, UINT32 limit, BOOL dos) {

	WCHAR *text = NULL;
	UINT32 len = 0;
	UINT8 *data = NULL;
	UINT32 bytes = 0;
	int count;
	BOOL present = FALSE;
	BOOL textavailable = FALSE;

	if (!OpenClipboard(clipwnd)) {
		return FALSE;
	}
	if (!dos && GetClipboardOwner() == clipwnd) {
		baseline();
		CloseClipboard();
		return TRUE;
	}
	if (clipmode & NPCL_H2G) {
		if (dos) {
			textavailable = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
					IsClipboardFormatAvailable(CF_TEXT) || IsClipboardFormatAvailable(CF_OEMTEXT);
		}
		readtext(&text, &len);
	}
	baseline();
	CloseClipboard();
	if (text) {
		if (format == NPCL_UTF16) {
			bytes = len * sizeof(WCHAR);
			if (bytes <= limit) {
				data = (UINT8 *)text;
				present = TRUE;
			}
		}
		else if (IsValidCodePage(cp)) {
			count = len ? WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, text, len, NULL, 0, "?", NULL) : 0;
			if ((!len || count > 0) && (UINT32)count <= limit) {
				bytes = count;
				if (!bytes) {
					present = TRUE;
				}
				else {
					data = (UINT8 *)malloc(bytes);
					if (data && WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, text, len,
											(LPSTR)data, count, "?", NULL) == count) {
						present = TRUE;
					}
				}
			}
		}
	}
	npclipio_lock();
	if (dos) {
		npclipio_dos_read_complete(session, present ? NPCL_OK : (textavailable ? NPCL_HOST_ERROR : NPCL_NO_DATA),
								 present ? data : NULL, present ? bytes : 0);
	}
	else {
		npclipio_publish(session, present, present ? data : NULL, present ? bytes : 0);
	}
	npclipio_unlock();
	if (data != (UINT8 *)text) {
		free(data);
	}
	free(text);
	return TRUE;
}

static HGLOBAL makeunicode(UINT8 format, UINT16 cp, const UINT8 *data, UINT32 len) {

	HGLOBAL h;
	WCHAR *p;
	int count;

	if (format == NPCL_UTF16) {
		count = len / sizeof(WCHAR);
	}
	else {
		if (!IsValidCodePage(cp)) {
			return NULL;
		}
		count = len ? MultiByteToWideChar(cp, 0, (LPCSTR)data, len, NULL, 0) : 0;
		if (len && count <= 0) {
			return NULL;
		}
	}
	h = GlobalAlloc(GMEM_MOVEABLE, (count + 1) * sizeof(WCHAR));
	if (!h) {
		return NULL;
	}
	p = (WCHAR *)GlobalLock(h);
	if (!p) {
		GlobalFree(h);
		return NULL;
	}
	if (format == NPCL_UTF16) {
		if (len) {
			memcpy(p, data, len);
		}
	}
	else if (count && MultiByteToWideChar(cp, 0, (LPCSTR)data, len, p, count) != count) {
		GlobalUnlock(h);
		GlobalFree(h);
		return NULL;
	}
	p[count] = 0;
	GlobalUnlock(h);
	return h;
}

// Windows 9xホストではANSI形式で設定する。
static HGLOBAL makeansi(HGLOBAL h) {

	const WCHAR *text;
	HGLOBAL out = NULL;
	char *p;
	int count;

	text = (const WCHAR *)GlobalLock(h);
	if (!text) {
		return NULL;
	}
	count = WideCharToMultiByte(CP_ACP, 0, text, -1, NULL, 0, NULL, NULL);
	if (count > 0) {
		out = GlobalAlloc(GMEM_MOVEABLE, count);
		if (out) {
			p = (char *)GlobalLock(out);
			if (!p) {
				GlobalFree(out);
				out = NULL;
			}
			else {
				int converted = WideCharToMultiByte(CP_ACP, 0, text, -1, p, count, NULL, NULL);
				GlobalUnlock(out);
				if (converted != count) {
					GlobalFree(out);
					out = NULL;
				}
			}
		}
	}
	GlobalUnlock(h);
	return out;
}

// Clipboard APIを呼び出す間は通信バッファをロックしない。
static BOOL acceptwrite(UINT32 session, UINT8 format, UINT16 cp) {

	UINT32 s, tx, gen, len;
	UINT32 s2, tx2, gen2, len2;
	const UINT8 *src;
	UINT8 *data = NULL;
	HGLOBAL h;
	BOOL valid;
	UINT8 result;
	UINT clipboardformat = CF_UNICODETEXT;

	npclipio_lock();
	valid = npclipio_peek_write(&s, &tx, &gen, &src, &len) && s == session;
	if (valid && len) {
		data = (UINT8 *)malloc(len);
		if (data) {
			memcpy(data, src, len);
		}
		else {
			npclipio_complete(s, tx, NPCL_HOST_ERROR);
			valid = FALSE;
		}
	}
	npclipio_unlock();
	if (!valid) {
		return TRUE;
	}
	h = makeunicode(format, cp, data, len);
	free(data);
	if (h && (GetVersion() & 0x80000000)) {
		HGLOBAL ansi = makeansi(h);
		GlobalFree(h);
		h = ansi;
		clipboardformat = CF_TEXT;
	}
	if (!h) {
		npclipio_lock();
		npclipio_complete(s, tx, NPCL_HOST_ERROR);
		npclipio_unlock();
		return TRUE;
	}
	if (!OpenClipboard(clipwnd)) {
		GlobalFree(h);
		return FALSE;
	}
	// 取得中にホストでコピーされた場合は先にその更新を処理する。
	if (changed() && GetClipboardOwner() != clipwnd) {
		CloseClipboard();
		GlobalFree(h);
		PostMessage(clipwnd, WM_NPCLIP_UPDATE, (WPARAM)clipsession, 0);
		return TRUE;
	}
	npclipio_lock();
	valid = npclipio_peek_write(&s2, &tx2, &gen2, &src, &len2) &&
			s == s2 && tx == tx2 && gen == gen2;
	npclipio_unlock();
	if (!valid) {
		CloseClipboard();
		GlobalFree(h);
		return TRUE;
	}
	result = NPCL_HOST_ERROR;
	if (EmptyClipboard()) {
		if (SetClipboardData(clipboardformat, h)) {
			h = NULL; // 所有権はWindowsへ移る。
			result = NPCL_OK;
		}
		baseline();
	}
	CloseClipboard();
	if (h) {
		GlobalFree(h);
	}
	npclipio_lock();
	npclipio_complete(s, tx, result);
	npclipio_unlock();
	return TRUE;
}

static void updateclip(void) {

	UINT32 session, limit;
	UINT8 format;
	UINT16 cp;
	BOOL connected;
	BOOL dos;
	BOOL readpending;
	BOOL ok = TRUE;

	if (!clipmode || !clipsession) {
		return;
	}
	if (busy) {
		again = TRUE;
		return;
	}
	busy = TRUE;
	npclipio_lock();
	connected = npclipio_format(&session, &format, &cp, &limit);
	dos = connected && npclipio_is_dos(session);
	readpending = dos && npclipio_dos_read_pending(session);
	npclipio_unlock();
	if (connected && clipsession == session) {
		if (dos) {
			if (readpending) {
				ok = publishtext(session, format, cp, limit, TRUE);
			}
			else if (changed()) {
				baseline();
			}
		}
		else if (changed()) {
			ok = publishtext(session, format, cp, limit, FALSE);
		}
		if (ok && (clipmode & NPCL_G2H)) {
			ok = acceptwrite(session, format, cp);
		}
	}
	// OpenClipboardが失敗した場合に限り再試行する。
	if (!ok) {
		if (!retry) {
			retry = (SetTimer(clipwnd, 1, 100, NULL) != 0);
		}
	}
	else if (retry) {
		KillTimer(clipwnd, 1);
		retry = FALSE;
	}
	busy = FALSE;
	if (again) {
		again = FALSE;
		PostMessage(clipwnd, WM_NPCLIP_UPDATE, (WPARAM)clipsession, 0);
	}
}

static void clearsession(UINT32 session) {

	if (retry) {
		KillTimer(clipwnd, 1);
		retry = FALSE;
	}
	clipsession = session;
	// 接続後に届く変更通知まで破棄しないよう、ここでは通知を取り除かない。
	clipseq = sequence();
	viewbase = viewseq;
}

static void stopmonitor(void) {

	if (clipwnd) {
		KillTimer(clipwnd, 1);
		retry = FALSE;
		if (listener) {
			removelistener(clipwnd);
			listener = FALSE;
		}
		if (viewer) {
			ChangeClipboardChain(clipwnd, nextviewer);
			viewer = FALSE;
			nextviewer = NULL;
		}
	}
}

static LRESULT CALLBACK clipproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	switch (msg) {
		case WM_NPCLIP_SESSION:
			if (clipmode) {
				clearsession((UINT32)wParam);
			}
			return 0;

		case WM_NPCLIP_UPDATE:
			if ((UINT32)wParam == clipsession) {
				updateclip();
			}
			return 0;

		case WM_TIMER:
			if (wParam == 1 && retry) {
				updateclip();
				return 0;
			}
			break;

		case WM_CLIPBOARDUPDATE:
			viewseq++;
			if (clipmode && clipsession) {
				PostMessage(hWnd, WM_NPCLIP_UPDATE, (WPARAM)clipsession, 0);
			}
			return 0;

		case WM_DRAWCLIPBOARD:
			viewseq++;
			if (clipmode && clipsession) {
				PostMessage(hWnd, WM_NPCLIP_UPDATE, (WPARAM)clipsession, 0);
			}
			if (nextviewer) {
				SendMessage(nextviewer, msg, wParam, lParam);
			}
			return 0;

		case WM_CHANGECBCHAIN:
			if ((HWND)wParam == nextviewer) {
				nextviewer = (HWND)lParam;
			}
			else if (nextviewer) {
				SendMessage(nextviewer, msg, wParam, lParam);
			}
			return 0;

		case WM_DESTROY:
			npclipio_set_notify(NULL);
			stopmonitor();
			clipwnd = NULL;
			clipmode = 0;
			npclipio_set_mode(0);
			return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void clipmng_initialize(HWND hWnd) {

#if !defined(NPCLIP_PHASE1_TEST)
	WNDCLASS wc;
	HMODULE user32 = GetModuleHandle(TEXT("user32.dll"));
#endif

	npclipio_initialize();
#if !defined(NPCLIP_PHASE1_TEST)
	getsequence = (DWORD (WINAPI *)(void))GetProcAddress(user32, "GetClipboardSequenceNumber");
	addlistener = (BOOL (WINAPI *)(HWND))GetProcAddress(user32, "AddClipboardFormatListener");
	removelistener = (BOOL (WINAPI *)(HWND))GetProcAddress(user32, "RemoveClipboardFormatListener");
	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = clipproc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = clipclass;
	if (RegisterClass(&wc)) {
		clipwnd = CreateWindow(clipclass, TEXT(""), WS_POPUP, 0, 0, 0, 0,
								hWnd, NULL, wc.hInstance, NULL);
	}
#endif
}

void clipmng_deinitialize(void) {

	if (clipwnd) {
		DestroyWindow(clipwnd);
	}
	UnregisterClass(clipclass, GetModuleHandle(NULL));
	npclipio_deinitialize();
}

BOOL clipmng_setmode(UINT8 mode) {

#if defined(NPCLIP_PHASE1_TEST)
	return mode == 0;
#else
	if (mode > 3 || (mode && !clipwnd)) {
		return FALSE;
	}
	if (mode == clipmode) {
		return TRUE;
	}
	if (mode && !clipmode) {
		if (addlistener && removelistener && addlistener(clipwnd)) {
			listener = TRUE;
		}
		else {
			nextviewer = SetClipboardViewer(clipwnd);
			viewer = TRUE;
		}
	}
	if (retry) {
		KillTimer(clipwnd, 1);
		retry = FALSE;
	}
	clipmode = mode;
	clipsession = 0;
	if (!mode) {
		npclipio_set_notify(NULL);
		stopmonitor();
	}
	else {
		baseline();
		npclipio_set_notify(clipwnd);
	}
	npclipio_set_mode(mode);
	return TRUE;
#endif
}
#endif
