#ifndef NPCLIPIO_H
#define NPCLIPIO_H

#define	NPCLIP_IO_STATUS		0x07f0
#define	NPCLIP_IO_COMMAND		0x07f0
#define	NPCLIP_IO_DATA			0x07f1
#define	NPCLIP_VERSION			0x0101
#define	NPCLIP_MAX_BYTES		0x100000UL
#define	NPCLIP_CAPS				0x003f


/* コマンド */
enum {
	NPCL_INFO = 0,
	NPCL_MODE,
	NPCL_FORMAT,
	NPCL_SEQUENCE,
	NPCL_READ_BEGIN = 0x10,
	NPCL_READ_END,
	NPCL_READ_ACK,
	NPCL_WRITE_BEGIN = 0x20,
	NPCL_WRITE_END,
	NPCL_RESULT,
	NPCL_DOS_OPEN = 0x30,
	NPCL_DOS_READ,
	NPCL_DOS_WRITE,
	NPCL_DOS_COMMIT,
	NPCL_DOS_RESULT,
	NPCL_DOS_CLOSE,
	NPCL_DOS_INFO,
	NPCL_ABORT = 0x7f
};

/* 結果コード */
enum {
	NPCL_OK = 0,
	NPCL_NO_DATA,
	NPCL_PENDING,
	NPCL_DISABLED = 0x80,
	NPCL_BAD_COMMAND,
	NPCL_BAD_STATE,
	NPCL_BAD_LENGTH,
	NPCL_BAD_FORMAT,
	NPCL_STALE_SESSION,
	NPCL_STALE_UPDATE,
	NPCL_BUSY,
	NPCL_NO_MEMORY,
	NPCL_HOST_ERROR,
	NPCL_ABORTED,
	NPCL_TIMEOUT,
	NPCL_UNKNOWN_TX
};

/* 共有方向・STATUSビット */
enum {
	NPCL_H2G = 1,
	NPCL_G2H = 2,
	NPCL_RX_PENDING = 4,
	NPCL_TX_READY = 8,
	NPCL_RX_ACTIVE = 16,
	NPCL_TX_ACTIVE = 32,
	NPCL_ERROR = 64,
	NPCL_PRESENT = 128
};

/* テキスト形式 */
enum {
	NPCL_UTF16 = 1,
	NPCL_ANSI,
	NPCL_OEM
};

#if defined(SUPPORT_NPCLIP)
#ifndef IOOUTCALL
#define IOOUTCALL
#endif
#ifndef IOINPCALL
#define IOINPCALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WINDOWS)
#define WM_NPCLIP_UPDATE	(WM_APP + 1)
#define WM_NPCLIP_SESSION	(WM_APP + 2)
void npclipio_initialize(void);
void npclipio_set_notify(HWND hWnd);
void npclipio_deinitialize(void);
void npclipio_lock(void);
void npclipio_unlock(void);
#endif
void npclipio_set_available(BOOL available);
BOOL npclipio_available(void);
void npclipio_reset(void);
void npclipio_set_mode(UINT8 mode);
/* 各関数は排他制御していないので必要ならすること。 */
BOOL npclipio_format(UINT32 *session, UINT8 *format, UINT16 *cp, UINT32 *limit);
/* data=NULL, len=0, present=FALSE は非テキスト更新。 */
UINT8 npclipio_publish(UINT32 session, BOOL present, const UINT8 *data, UINT32 len);
/* ポインタの寿命は次の状態変更まで。非同期側へ渡す場合はコピーする。 */
BOOL npclipio_peek_write(UINT32 *session, UINT32 *tx, UINT32 *generation,
						 const UINT8 **data, UINT32 *len);
BOOL npclipio_complete(UINT32 session, UINT32 tx, UINT8 result);

BOOL npclipio_is_dos(UINT32 session);
BOOL npclipio_dos_read_pending(UINT32 session);
void npclipio_dos_read_complete(UINT32 session, UINT8 result, const UINT8 *data, UINT32 len);

/* I/Oハンドラ */
REG8 IOINPCALL npclipio_status(UINT port);
REG8 IOINPCALL npclipio_read(UINT port);
void IOOUTCALL npclipio_command(UINT port, REG8 value);
void IOOUTCALL npclipio_write(UINT port, REG8 value);
#ifdef __cplusplus
}
#endif
#endif
#endif
