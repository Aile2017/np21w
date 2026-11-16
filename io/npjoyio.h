#ifndef NPJOYIO_H
#define NPJOYIO_H

/* NPJOY専用I/Oプロトコル v3。8bit INのみ。 */
#define NPJOY_IO_STATUS     0x07e3
#define NPJOY_IO_DATA       0x07e5
/* STATUS: 上位4bitは接続状態、下位4bitはボタン。 */
#define NPJOY_STATUS_MASK   0xf0
#define NPJOY_STATUS_OFF    0xa0
#define NPJOY_STATUS_ON     0xb0
#define NPJOY_BUTTON_MASK   0x0f
/* DATA: X下位・上位、Y下位・上位、識別子3バイト。 */
#define NPJOY_DATA_LEN      7
#define NPJOY_MAGIC0        0x4e
#define NPJOY_MAGIC1        0x4a
#define NPJOY_MAGIC2        0x03
/* 診断ログ用の状態値（STATUSポートの値とは別）。 */
#define NPJOY_DISCONNECTED  0xa0
#define NPJOY_CONNECTED     0xa1
#define NPJOY_CENTER        0x8000

#endif
