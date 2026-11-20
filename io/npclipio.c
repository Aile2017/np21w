#include	"compiler.h"
#include	"npclipio.h"
#if defined(SUPPORT_NPCLIP)
#include	<stdlib.h>
#include	<string.h>

enum {
	IDLE,
	RX_BODY,
	RX_END,
	RX_ACK,
	TX_BODY,
	TX_END,
	TX_WAIT,
	DOS_RX_BODY,
	DOS_TX_BODY
};

enum {
	DOS_CLOSED,
	DOS_OPENED,
	DOS_READ_WAIT,
	DOS_READ_READY,
	DOS_WRITING,
	DOS_COMMITTED
};

static struct {
	BOOL	available;
	UINT8	mode;
	UINT8	format;
	UINT8	state;
	UINT8	error;
	UINT8	cmd;
	UINT8	need;
	UINT8	got;
	UINT16	cp;
	UINT32	session;
	UINT32	generation;
	UINT32	hostid;
	UINT32	consumed;
	UINT32	limit;
	UINT32	rxid;
	UINT32	rxgen;
	UINT32	rxlen;
	UINT32	rxpos;
	UINT32	txid;
	UINT32	txbase;
	UINT32	txlen;
	UINT32	txpos;
	UINT32	lasttx;
	UINT32	resultid;
	UINT32	resultgen;
	UINT8	result;
	BOOL	dos;
	UINT8	dosstate;
	UINT8	dosresult;
	UINT32	dosend;
	UINT32	doscapacity;
	UINT8	*host;
	UINT8	*rx;
	UINT8	*tx;
	UINT32	hostlen;
	UINT8	param[16];
	UINT8	reply[16];
	UINT8	rlen;
	UINT8	rpos;
} cl;

#if defined(_WINDOWS)
static CRITICAL_SECTION clipcs;
static BOOL clipcsinit;
static HWND notifywnd;

void npclipio_initialize(void) {

	InitializeCriticalSection(&clipcs);
	clipcsinit = TRUE;
}

void npclipio_set_notify(HWND hWnd) {

	npclipio_lock();
	notifywnd = hWnd;
	npclipio_unlock();
}

static void notify(UINT msg, UINT32 session) {

	if (notifywnd) {
		PostMessage(notifywnd, msg, (WPARAM)session, 0);
	}
}

void npclipio_deinitialize(void) {

	if (clipcsinit) {
		DeleteCriticalSection(&clipcs);
		clipcsinit = FALSE;
	}
}

void npclipio_lock(void) {

	if (clipcsinit) {
		EnterCriticalSection(&clipcs);
	}
}

void npclipio_unlock(void) {

	if (clipcsinit) {
		LeaveCriticalSection(&clipcs);
	}
}
#else
#define notify(msg, session)	((void)0)
#define npclipio_lock()		((void)0)
#define npclipio_unlock()	((void)0)
#endif

/* ---- データ変換・応答 */

static UINT32 rd32(const UINT8 *p) {

	return (UINT32)p[0] | ((UINT32)p[1] << 8) |
		   ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT16 rd16(const UINT8 *p) {

	return (UINT16)(p[0] | (p[1] << 8));
}

static void wr8(UINT8 v) {

	cl.reply[cl.rlen++] = v;
}

static void wr16(UINT16 v) {

	wr8((UINT8)v);
	wr8((UINT8)(v >> 8));
}

static void wr32(UINT32 v) {

	wr16((UINT16)v);
	wr16((UINT16)(v >> 16));
}

static void response(UINT8 v) {

	cl.rpos = cl.rlen = 0;
	wr8(v);
	cl.error = (v >= 0x80);
}

/* ---- 転送・セッション管理 */

static void rxclear(void) {

	free(cl.rx);
	cl.rx = NULL;
	cl.rxlen = cl.rxpos = 0;
}

static void txclear(void) {

	free(cl.tx);
	cl.tx = NULL;
	cl.txlen = cl.txpos = 0;
}

static void abort_transfer(UINT8 result) {

	if (cl.state == TX_WAIT) {
		cl.result = result;
	}
	rxclear();
	txclear();
	cl.state = IDLE;
	cl.dosstate = DOS_CLOSED;
	cl.doscapacity = 0;
	cl.need = cl.got = cl.rlen = cl.rpos = 0;
}

static void fail(UINT8 result) {

	abort_transfer(result);
	response(result);
}

static void new_session(void) {

	abort_transfer(NPCL_ABORTED);
	free(cl.host);
	cl.host = NULL;
	cl.hostlen = 0;
	if (++cl.session == 0) {
		++cl.session;
	}
	cl.generation = cl.hostid = cl.consumed = 0;
	cl.lasttx = cl.resultid = cl.resultgen = 0;
	cl.dos = FALSE;
	cl.format = 0;
	cl.cp = 0;
	cl.limit = 0;
	cl.error = 0;
	notify(WM_NPCLIP_SESSION, 0);
}

void npclipio_reset(void) {

	npclipio_lock();
	new_session();
	npclipio_unlock();
}

void npclipio_set_available(BOOL available) {

	npclipio_lock();
	new_session();
	cl.available = available;
#if defined(NPCLIP_PHASE1_TEST)
	/* 明示的な試験用ビルドのみ。実ホストのClipboardには接続しない。 */
	cl.mode = 3;
#endif
	npclipio_unlock();
}

BOOL npclipio_available(void) {

	BOOL ret;

	npclipio_lock();
	ret = cl.available;
	npclipio_unlock();
	return ret;
}

void npclipio_set_mode(UINT8 mode) {

	npclipio_lock();
	if (mode <= 3 && cl.mode != mode) {
		cl.mode = mode;
		new_session();
	}
	npclipio_unlock();
}

static BOOL advance(void) {

	if (cl.generation == 0xffffffffUL) {
		new_session();
		return FALSE;
	}
	++cl.generation;
	return TRUE;
}

static UINT8 validate(const UINT8 *data, UINT32 len) {

	UINT32	i;
	UINT16	c;
	UINT16	d;

	if (len > cl.limit || (cl.format == NPCL_UTF16 && (len & 1))) {
		return NPCL_BAD_LENGTH;
	}
	if (len && !data) {
		return NPCL_BAD_FORMAT;
	}
	if (cl.format == NPCL_UTF16) {
		/* NUL終端は転送しない。サロゲート対も検査する。 */
		for (i = 0; i < len; i += 2) {
			c = rd16(data + i);
			if (!c || (c >= 0xdc00 && c <= 0xdfff)) {
				return NPCL_BAD_FORMAT;
			}
			if (c >= 0xd800 && c <= 0xdbff) {
				i += 2;
				if (i >= len) {
					return NPCL_BAD_FORMAT;
				}
				d = rd16(data + i);
				if (d < 0xdc00 || d > 0xdfff) {
					return NPCL_BAD_FORMAT;
				}
			}
		}
	}
	else {
		for (i = 0; i < len; ++i) {
			if (!data[i]) {
				return NPCL_BAD_FORMAT;
			}
		}
	}
	return NPCL_OK;
}

/* ---- ホスト側インターフェース */

BOOL npclipio_format(UINT32 *session, UINT8 *format, UINT16 *cp, UINT32 *limit) {

	if (!cl.available || !cl.format) {
		return FALSE;
	}
	*session = cl.session;
	*format = cl.format;
	*cp = cl.cp;
	*limit = cl.limit;
	return TRUE;
}

UINT8 npclipio_publish(UINT32 session, BOOL present, const UINT8 *data, UINT32 len) {

	UINT8	rc = NPCL_OK;
	UINT8	*p = NULL;

	if (session != cl.session) {
		return NPCL_STALE_SESSION;
	}
	if (!cl.available || !cl.mode) {
		return NPCL_DISABLED;
	}
	if (!cl.format) {
		return NPCL_BAD_STATE;
	}
	/* G2Hのみの場合でも外部更新は保留書込みを失効させる。 */
	if (present && (cl.mode & NPCL_H2G)) {
		rc = validate(data, len);
		if (rc == NPCL_OK && len) {
			p = (UINT8 *)malloc(len);
			if (!p) {
				rc = NPCL_NO_MEMORY;
			}
			else {
				memcpy(p, data, len);
			}
		}
	}
	if (!advance()) {
		free(p);
		return NPCL_STALE_SESSION;
	}
	free(cl.host);
	cl.host = p;
	cl.hostlen = 0;
	cl.hostid = 0;
	if (present && (cl.mode & NPCL_H2G) && rc == NPCL_OK) {
		cl.hostid = cl.generation;
		cl.hostlen = len;
	}
	if (cl.state == TX_WAIT) {
		txclear();
		cl.state = IDLE;
		cl.result = NPCL_STALE_UPDATE;
	}
	return rc;
}

BOOL npclipio_peek_write(UINT32 *session, UINT32 *tx, UINT32 *generation,
						 const UINT8 **data, UINT32 *len) {

	if (cl.state != TX_WAIT || !cl.available || !(cl.mode & NPCL_G2H)) {
		return FALSE;
	}
	*session = cl.session;
	*tx = cl.resultid;
	*generation = cl.resultgen;
	*data = cl.tx;
	*len = cl.txlen;
	return TRUE;
}

BOOL npclipio_complete(UINT32 session, UINT32 tx, UINT8 result) {

	if (session != cl.session || cl.state != TX_WAIT || tx != cl.resultid) {
		return FALSE;
	}
	if (result != NPCL_OK && result != NPCL_HOST_ERROR && result != NPCL_STALE_UPDATE &&
		result != NPCL_ABORTED && result != NPCL_BAD_FORMAT) {
		return FALSE;
	}
	cl.result = result;
	txclear();
	cl.state = IDLE;
	return TRUE;
}

BOOL npclipio_is_dos(UINT32 session) {

	return cl.dos && cl.session == session;
}

BOOL npclipio_dos_read_pending(UINT32 session) {

	return npclipio_is_dos(session) && cl.dosstate == DOS_READ_WAIT && (cl.mode & NPCL_H2G);
}

void npclipio_dos_read_complete(UINT32 session, UINT8 result, const UINT8 *data, UINT32 len) {

	if (!npclipio_dos_read_pending(session)) {
		return;
	}
	if (result == NPCL_OK) {
		result = validate(data, len);
		if (result == NPCL_OK && len) {
			cl.rx = (UINT8 *)malloc(len);
			if (cl.rx) {
				memcpy(cl.rx, data, len);
			}
			else {
				result = NPCL_NO_MEMORY;
			}
		}
	}
	cl.rxlen = (result == NPCL_OK) ? len : 0;
	cl.rxpos = 0;
	cl.dosresult = result;
	cl.dosstate = DOS_READ_READY;
}

/* DOSの一回のOPENに対して、読み出し元または書き込み先を一つ保持する。 */
static void doscommand(UINT32 session) {

	UINT32 len;
	UINT32 capacity;
	UINT8 *p;
	UINT8 rc;

	if (cl.cmd == NPCL_DOS_OPEN) {
		if (!cl.mode) {
			response(NPCL_DISABLED);
			return;
		}
		new_session();
		cl.dos = TRUE;
		cl.dosstate = DOS_OPENED;
		cl.format = NPCL_ANSI;
		cl.cp = 932;
		cl.limit = NPCLIP_MAX_BYTES;
		response(NPCL_OK);
		wr32(cl.session);
		notify(WM_NPCLIP_SESSION, cl.session);
		return;
	}
	if (session != cl.session || !cl.dos) {
		response(NPCL_STALE_SESSION);
		return;
	}
	if (cl.cmd == NPCL_DOS_CLOSE) {
		abort_transfer(NPCL_ABORTED);
		response(NPCL_OK);
		return;
	}
	if (cl.dosstate == DOS_CLOSED) {
		response(NPCL_BAD_STATE);
		return;
	}
	switch (cl.cmd) {
		case NPCL_DOS_READ:
			if (!(cl.mode & NPCL_H2G)) {
				response(NPCL_DISABLED);
				break;
			}
			if (cl.dosstate == DOS_OPENED) {
				cl.dosstate = DOS_READ_WAIT;
				notify(WM_NPCLIP_UPDATE, cl.session);
			}
			if (cl.dosstate == DOS_READ_WAIT) {
				response(NPCL_PENDING);
				break;
			}
			if (cl.dosstate != DOS_READ_READY) {
				response(NPCL_BAD_STATE);
				break;
			}
			if (cl.dosresult != NPCL_OK) {
				response(cl.dosresult);
				break;
			}
			len = rd16(cl.param + 4);
			if (len > cl.rxlen - cl.rxpos) {
				len = cl.rxlen - cl.rxpos;
			}
			cl.dosend = cl.rxpos + len;
			cl.state = len ? DOS_RX_BODY : IDLE;
			response(NPCL_OK);
			wr16((UINT16)len);
			break;

		case NPCL_DOS_WRITE:
			if (!(cl.mode & NPCL_G2H)) {
				response(NPCL_DISABLED);
				break;
			}
			if (cl.dosstate != DOS_OPENED && cl.dosstate != DOS_WRITING) {
				response(NPCL_BAD_STATE);
				break;
			}
			len = rd16(cl.param + 4);
			if (len > cl.limit - cl.txlen) {
				fail(NPCL_BAD_LENGTH);
				break;
			}
			if (cl.txlen + len > cl.doscapacity) {
				capacity = cl.doscapacity ? cl.doscapacity : 256;
				while (capacity < cl.txlen + len) {
					capacity *= 2;
				}
				p = (UINT8 *)realloc(cl.tx, capacity);
				if (!p) {
					fail(NPCL_NO_MEMORY);
					break;
				}
				cl.tx = p;
				cl.doscapacity = capacity;
			}
			cl.dosstate = DOS_WRITING;
			cl.txlen += len;
			cl.state = len ? DOS_TX_BODY : IDLE;
			response(NPCL_OK);
			break;

		case NPCL_DOS_COMMIT:
			if (!(cl.mode & NPCL_G2H) || cl.dosstate != DOS_WRITING) {
				response(NPCL_BAD_STATE);
				break;
			}
			/* DOSのテキスト終端記号はホストに渡さない。 */
			if (cl.txlen && cl.tx[cl.txlen - 1] == 0x1a) {
				cl.txlen--;
			}
			rc = validate(cl.tx, cl.txlen);
			if (rc != NPCL_OK) {
				fail(rc);
				break;
			}
			if (!advance()) {
				response(NPCL_STALE_SESSION);
				break;
			}
			cl.dosstate = DOS_COMMITTED;
			cl.resultid = 1;
			cl.resultgen = cl.generation;
			cl.result = NPCL_PENDING;
			cl.state = TX_WAIT;
			response(NPCL_OK);
			notify(WM_NPCLIP_UPDATE, cl.session);
			break;

		case NPCL_DOS_RESULT:
			response(cl.dosstate == DOS_COMMITTED ? cl.result : NPCL_BAD_STATE);
			break;
	}
}

/* ---- 状態 */

static BOOL pending(void) {

	return !cl.dos && cl.format && (cl.mode & NPCL_H2G) && cl.hostid && cl.hostid != cl.consumed;
}

static BOOL ready(void) {

	return !cl.dos && cl.format && (cl.mode & NPCL_G2H) && cl.state == IDLE && !cl.need && cl.rpos == cl.rlen;
}

#if defined(NPCLIP_PHASE1_TEST)
static void fixture(void) {

	static const UINT8 ascii[] = "NPCL phase1\r\n";
	UINT8	wide[(sizeof(ascii) - 1) * 2];
	UINT	i;

	if (cl.format == NPCL_UTF16) {
		for (i = 0; i < sizeof(ascii) - 1; ++i) {
			wide[i * 2] = ascii[i];
			wide[i * 2 + 1] = 0;
		}
		npclipio_publish(cl.session, TRUE, wide, sizeof(wide));
	}
	else {
		npclipio_publish(cl.session, TRUE, ascii, sizeof(ascii) - 1);
	}
}

#endif

/* ---- コマンド処理 */

static void execute(void) {

	UINT32	s = rd32(cl.param);
	UINT32	id = rd32(cl.param + 4);
	UINT32	g;
	UINT32	len;
	UINT16	cp;
	UINT8	f;
	UINT8	rc;

	cl.need = cl.got = 0;
	if (cl.cmd == NPCL_DOS_INFO) {
		response(NPCL_OK);
		wr8('D');
		wr8('O');
		wr8('S');
		wr8(1);
		return;
	}
	if (cl.cmd >= NPCL_DOS_OPEN && cl.cmd <= NPCL_DOS_CLOSE) {
		doscommand(s);
		return;
	}
	if (cl.cmd == NPCL_INFO) {
		response(NPCL_OK);
		wr8('N');
		wr8('P');
		wr8('C');
		wr8('L');
		wr16(NPCLIP_VERSION);
		wr16(NPCLIP_CAPS);
		wr32(NPCLIP_MAX_BYTES);
		return;
	}
	if (cl.cmd == NPCL_MODE) {
		response(NPCL_OK);
		wr8(cl.mode);
		return;
	}
	if (cl.cmd == NPCL_SEQUENCE) {
		response(NPCL_OK);
		wr32(cl.session);
		wr32(cl.generation);
		wr32(cl.hostid);
		return;
	}
	if (s != cl.session) {
		fail(NPCL_STALE_SESSION);
		return;
	}
	if (cl.cmd == NPCL_FORMAT) {
		f = cl.param[4];
		cp = rd16(cl.param + 5);
		len = rd32(cl.param + 7);
		if ((f == NPCL_UTF16 && cp != 1200) ||
			((f == NPCL_ANSI || f == NPCL_OEM) &&
			 (cp <= 3 || cp == 1200 || cp == 1201 || cp == 65000 || cp == 65001)) ||
			f < 1 || f > 3) {
			fail(NPCL_BAD_FORMAT);
			return;
		}
		if (!len) {
			fail(NPCL_BAD_LENGTH);
			return;
		}
		new_session();
		cl.format = f;
		cl.cp = cp;
		cl.limit = len > NPCLIP_MAX_BYTES ? NPCLIP_MAX_BYTES : len;
		response(NPCL_OK);
		wr32(cl.session);
		wr8(f);
		wr16(cp);
		wr32(cl.limit);
		notify(WM_NPCLIP_SESSION, cl.session);
#if defined(NPCLIP_PHASE1_TEST)
		if (cl.mode & NPCL_H2G) {
			fixture();
		}
#endif
		return;
	}
	if (cl.dos) {
		response(NPCL_BAD_STATE);
		return;
	}
	if (cl.cmd >= NPCL_READ_BEGIN && cl.cmd <= NPCL_READ_ACK && !(cl.mode & NPCL_H2G)) {
		fail(NPCL_DISABLED);
		return;
	}
	if (cl.cmd >= NPCL_WRITE_BEGIN && cl.cmd <= NPCL_RESULT && !(cl.mode & NPCL_G2H)) {
		fail(NPCL_DISABLED);
		return;
	}
	if (!cl.format) {
		fail(NPCL_BAD_STATE);
		return;
	}
	switch (cl.cmd) {
		case NPCL_READ_BEGIN:
			if (!pending()) {
				response(NPCL_NO_DATA);
				return;
			}
			if (cl.hostlen) {
				cl.rx = (UINT8 *)malloc(cl.hostlen);
				if (!cl.rx) {
					fail(NPCL_NO_MEMORY);
					return;
				}
				memcpy(cl.rx, cl.host, cl.hostlen);
			}
			cl.rxlen = cl.hostlen;
			cl.rxpos = 0;
			cl.rxid = cl.hostid;
			cl.rxgen = cl.generation;
			cl.state = cl.rxlen ? RX_BODY : RX_END;
			response(NPCL_OK);
			wr32(cl.rxid);
			wr32(cl.rxgen);
			wr32(cl.rxlen);
			break;

		case NPCL_READ_END:
			if (id != cl.rxid) {
				fail(NPCL_UNKNOWN_TX);
				break;
			}
			if (cl.rxid != cl.hostid || cl.rxgen != cl.generation) {
				fail(NPCL_STALE_UPDATE);
				break;
			}
			rxclear();
			cl.state = RX_ACK;
			response(NPCL_OK);
			break;

		case NPCL_READ_ACK:
			if (id != cl.rxid) {
				fail(NPCL_UNKNOWN_TX);
				break;
			}
			if (id != cl.hostid || cl.rxgen != cl.generation) {
				fail(NPCL_STALE_UPDATE);
				break;
			}
			if (cl.param[8] > 2) {
				fail(NPCL_BAD_FORMAT);
				break;
			}
			if (cl.param[8] != 2) {
				cl.consumed = id;
			}
			cl.state = IDLE;
			response(NPCL_OK);
			break;

		case NPCL_WRITE_BEGIN:
			g = rd32(cl.param + 8);
			len = rd32(cl.param + 12);
			if (!id || id <= cl.lasttx) {
				fail(NPCL_UNKNOWN_TX);
				break;
			}
			if (g != cl.generation) {
				fail(NPCL_STALE_UPDATE);
				break;
			}
			if (len > cl.limit || (cl.format == NPCL_UTF16 && (len & 1))) {
				fail(NPCL_BAD_LENGTH);
				break;
			}
			if (len) {
				cl.tx = (UINT8 *)malloc(len);
				if (!cl.tx) {
					fail(NPCL_NO_MEMORY);
					break;
				}
			}
			cl.lasttx = cl.txid = id;
			cl.txbase = g;
			cl.txlen = len;
			cl.txpos = 0;
			cl.resultid = 0;
			cl.state = len ? TX_BODY : TX_END;
			response(NPCL_OK);
			break;

		case NPCL_WRITE_END:
			if (id != cl.txid) {
				fail(NPCL_UNKNOWN_TX);
				break;
			}
			if (cl.txbase != cl.generation) {
				fail(NPCL_STALE_UPDATE);
				break;
			}
			rc = validate(cl.tx, cl.txlen);
			if (rc != NPCL_OK) {
				fail(rc);
				break;
			}
			if (!advance()) {
				response(NPCL_STALE_SESSION);
				break;
			}
			/* 旧Hを消費扱いにし、ゲスト自身のコピーを戻さない。 */
			cl.consumed = cl.hostid;
			cl.resultid = id;
			cl.resultgen = cl.generation;
			cl.result = NPCL_PENDING;
			cl.state = TX_WAIT;
			response(NPCL_OK);
			notify(WM_NPCLIP_UPDATE, cl.session);
			break;

		case NPCL_RESULT:
			if (!id || id != cl.resultid) {
				response(NPCL_UNKNOWN_TX);
				break;
			}
			response(cl.result);
			if (cl.result == NPCL_OK) {
				wr32(cl.resultgen);
			}
#if defined(NPCLIP_PHASE1_TEST)
			/* 初回はPENDINGを返し、次回から完了。 */
			if (cl.result == NPCL_PENDING) {
				UINT8	*echo = NULL;
				UINT32	size = cl.txlen;

				if (size) {
					echo = (UINT8 *)malloc(size);
					if (echo) {
						memcpy(echo, cl.tx, size);
					}
				}
				if (size && !echo) {
					npclipio_complete(cl.session, id, NPCL_HOST_ERROR);
				}
				else {
					npclipio_complete(cl.session, id, NPCL_OK);
					/* 診断用のエコー。通常ビルドには含めない。 */
					if (cl.mode & NPCL_H2G) {
						npclipio_publish(cl.session, TRUE, echo, size);
					}
				}
				free(echo);
			}
#endif
			break;
	}
}

/* ---- I/O */

static REG8 clip_status(UINT port) {

	UINT8	v;

	(void)port;
	if (!cl.available) {
		return 0xff;
	}
	v = NPCL_PRESENT | cl.mode;
	if (cl.error) {
		v |= NPCL_ERROR;
	}
	if (pending()) {
		v |= NPCL_RX_PENDING;
	}
	if (ready()) {
		v |= NPCL_TX_READY;
	}
	if ((cl.state >= RX_BODY && cl.state <= RX_ACK) || cl.state == DOS_RX_BODY) {
		v |= NPCL_RX_ACTIVE;
	}
	if ((cl.state >= TX_BODY && cl.state <= TX_WAIT) || cl.state == DOS_TX_BODY) {
		v |= NPCL_TX_ACTIVE;
	}
	return v;
}

static void clip_command(UINT port, REG8 value) {

	UINT8	n;

	(void)port;
	if (!cl.available) {
		return;
	}
	if (value == NPCL_ABORT) {
		abort_transfer(NPCL_ABORTED);
		response(NPCL_OK);
		return;
	}
	if (cl.need || cl.rpos != cl.rlen) {
		fail(NPCL_BAD_STATE);
		return;
	}
	switch (value) {
		case NPCL_INFO:
		case NPCL_MODE:
		case NPCL_SEQUENCE:
		case NPCL_DOS_OPEN:
		case NPCL_DOS_INFO:
			n = 0;
			break;

		case NPCL_FORMAT:
			n = 11;
			break;

		case NPCL_READ_BEGIN:
		case NPCL_DOS_COMMIT:
		case NPCL_DOS_RESULT:
		case NPCL_DOS_CLOSE:
			n = 4;
			break;

		case NPCL_DOS_READ:
		case NPCL_DOS_WRITE:
			n = 6;
			break;

		case NPCL_READ_END:
		case NPCL_WRITE_END:
		case NPCL_RESULT:
			n = 8;
			break;

		case NPCL_READ_ACK:
			n = 9;
			break;

		case NPCL_WRITE_BEGIN:
			n = 16;
			break;
		default:
			fail(NPCL_BAD_COMMAND);
			return;
	}
	if ((cl.state == RX_BODY || cl.state == TX_BODY || cl.state == DOS_RX_BODY || cl.state == DOS_TX_BODY) ||
		(cl.state == RX_END && value != NPCL_READ_END) ||
		(cl.state == TX_END && value != NPCL_WRITE_END) ||
		(cl.state == RX_ACK && value != NPCL_READ_ACK && value != NPCL_SEQUENCE) ||
		(cl.state == TX_WAIT && value != NPCL_RESULT && value != NPCL_SEQUENCE && value != NPCL_MODE &&
		 value != NPCL_DOS_RESULT && value != NPCL_DOS_CLOSE) ||
		(cl.state != RX_END && value == NPCL_READ_END) ||
		(cl.state != RX_ACK && value == NPCL_READ_ACK) ||
		(cl.state != TX_END && value == NPCL_WRITE_END)) {
		fail(NPCL_BAD_STATE);
		return;
	}
	cl.cmd = (UINT8)value;
	cl.need = n;
	cl.got = 0;
	if (!n) {
		execute();
	}
}

static REG8 clip_read(UINT port) {

	(void)port;
	if (!cl.available) {
		return 0xff;
	}
	if (cl.rpos < cl.rlen) {
		return cl.reply[cl.rpos++];
	}
	if (!cl.need && cl.state == DOS_RX_BODY) {
		UINT8 v = cl.rx[cl.rxpos++];

		if (cl.rxpos == cl.dosend) {
			cl.state = IDLE;
		}
		return v;
	}
	if (!cl.need && cl.state == RX_BODY) {
		UINT8	v = cl.rx[cl.rxpos++];

		if (cl.rxpos == cl.rxlen) {
			cl.state = RX_END;
		}
		return v;
	}
	abort_transfer(NPCL_BAD_STATE);
	cl.error = 1;
	return 0xff;
}

static void clip_write(UINT port, REG8 value) {

	(void)port;
	if (!cl.available) {
		return;
	}
	if (cl.rpos != cl.rlen) {
		fail(NPCL_BAD_STATE);
		return;
	}
	if (cl.need) {
		cl.param[cl.got++] = (UINT8)value;
		if (cl.got == cl.need) {
			execute();
		}
	}
	else if (cl.state == DOS_TX_BODY) {
		cl.tx[cl.txpos++] = (UINT8)value;
		if (cl.txpos == cl.txlen) {
			cl.state = IDLE;
		}
	}
	else if (cl.state == TX_BODY) {
		cl.tx[cl.txpos++] = (UINT8)value;
		if (cl.txpos == cl.txlen) {
			cl.state = TX_END;
		}
	}
	else {
		fail(NPCL_BAD_STATE);
	}
}

REG8 IOINPCALL npclipio_status(UINT port) {

	REG8 ret;

	npclipio_lock();
	ret = clip_status(port);
	npclipio_unlock();
	return ret;
}

REG8 IOINPCALL npclipio_read(UINT port) {

	REG8 ret;

	npclipio_lock();
	ret = clip_read(port);
	npclipio_unlock();
	return ret;
}

void IOOUTCALL npclipio_command(UINT port, REG8 value) {

	npclipio_lock();
	clip_command(port, value);
	npclipio_unlock();
}

void IOOUTCALL npclipio_write(UINT port, REG8 value) {

	npclipio_lock();
	clip_write(port, value);
	npclipio_unlock();
}

#endif
