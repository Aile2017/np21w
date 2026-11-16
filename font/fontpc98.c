#include	"compiler.h"
#include	"bmpdata.h"
#include	"dosio.h"
#include	"cpucore.h"
#include	"font.h"
#include	"fontdata.h"


#define	FONTYSIZE		16

#define	BMPWIDTH		2048L
#define	BMPHEIGHT		2048L

#define	BMPLINESIZE		(BMPWIDTH / 8)			// 割り切れる^^;

#define	BMPDATASIZE		(BMPLINESIZE * BMPHEIGHT)

// 通常読み込みと差分適用で共通の漢字コード範囲（終端は含まない）
#define	FONTKNJ1_START	0x01
#define	FONTKNJ1_END	0x30
#define	FONTKNJ2_END	0x56
#define	FONTKNJ3_START	0x58
#define	FONTKNJ3_END	0x60
#define	FONTKNJ_OVERLAY_PAGE	0x08
#define	FONTKNJ_PAGE_END	0x80


static void pc98ankcpy(UINT8 *dst, const UINT8 *src, int from, int to, BOOL overlay) {

	int		y;
const UINT8	*p;
	int		ank;
	UINT8	glyph[FONTYSIZE];
	UINT8	bits;

	for (ank=from; ank<to; ank++) {

		// ANKフォントのスタート位置
		p = src + BMPDATASIZE - BMPLINESIZE + ank;
		bits = 0;
		for (y=0; y<FONTYSIZE; y++) {
			glyph[y] = (UINT8)~p[-y * BMPLINESIZE];
			bits |= glyph[y];
		}
		if (!overlay || bits) {
			CopyMemory(dst, glyph, FONTYSIZE);
		}
		dst += FONTYSIZE;
	}
}

static void pc98knjcpy(UINT8 *dst, const UINT8 *src, int from, int to, BOOL overlay) {

	int		i, j, k;
const UINT8	*p;
	UINT8	*q;
	UINT8	glyph[32];
	UINT8	bits;

	for (i=from; i<to; i++) {
		p = src + (BMPDATASIZE + (i << 1) - ((FONTYSIZE + 1) * BMPLINESIZE));
		q = dst + 0x1000 + (i << 4);
		for (j=1; j<FONTKNJ_PAGE_END; j++) {
			bits = 0;
			for (k=0; k<FONTYSIZE; k++) {
				glyph[k] = (UINT8)~p[-k * BMPLINESIZE];
				glyph[k + FONTYSIZE] = (UINT8)~p[1 - k * BMPLINESIZE];
				bits |= glyph[k] | glyph[k + FONTYSIZE];
			}
			// 先頭8ページはpccore_reset()でクリアされる
			if (!overlay || ((j >= FONTKNJ_OVERLAY_PAGE) && bits)) {
				CopyMemory(q, glyph, FONTYSIZE);
				CopyMemory(q + 0x800, glyph + FONTYSIZE, FONTYSIZE);
			}
			if (j < FONTKNJ_PAGE_END - 1) {
				p -= FONTYSIZE * BMPLINESIZE;
				q += 0x1000;
			}
		}
	}
}

// RGBを白(1)または黒(0)のBMPデータに変換（ディザリングなし）
static UINT8 pc98monobit(UINT r, UINT g, UINT b) {

	return((UINT8)((r * 77 + g * 150 + b * 29) >= (128 * 256)));
}

static UINT pc98expand5(UINT value) {

	return((value << 3) | (value >> 2));
}

// 多色BMPの1ラインをモノクロに変換する
// パレット形式の場合は存在しない色番号をエラーとする
static BRESULT pc98monoline(UINT8 *dst, const UINT8 *src, int bpp,
							const UINT8 *mono, UINT colors) {

	UINT x, index, pixel;
	UINT8 bits, white;

	bits = 0;
	for (x=0; x<BMPWIDTH; x++) {
		switch (bpp) {
		case 4:
			index = (src[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
			if (index >= colors) {
				return(FAILURE);
			}
			white = mono[index];
			break;
		case 8:
			index = src[x];
			if (index >= colors) {
				return(FAILURE);
			}
			white = mono[index];
			break;
		case 16:
			// BI_RGBの16bppはRGB555形式（最上位ビットは未使用）
			pixel = LOADINTELWORD(src + x * 2);
			white = pc98monobit(pc98expand5((pixel >> 10) & 31),
					pc98expand5((pixel >> 5) & 31), pc98expand5(pixel & 31));
			break;
		case 24:
			white = pc98monobit(src[x * 3 + 2], src[x * 3 + 1], src[x * 3]);
			break;
		case 32:
			// BI_RGBの32bppはBGR順（4バイト目は無視）
			white = pc98monobit(src[x * 4 + 2], src[x * 4 + 1], src[x * 4]);
			break;
		default:
			return(FAILURE);
		}
		bits = (UINT8)((bits << 1) | white);
		if ((x & 7) == 7) {
			*dst++ = bits;
			bits = 0;
		}
	}
	return(SUCCESS);
}

static UINT8 *pc98readbmp(const OEMCHAR *filename, BOOL overlay) {

	FILEH	fh;
	BMPFILE	bf;
	BMPINFO	bi;
	UINT8	*bmpdata = NULL;
	UINT8	*row = NULL;
	UINT8	*dst;
	BMPDATA	bd;
	long	fptr;
	UINT8	palette[256 * 4];
	UINT8	mono[256];
	BOOL	reverse = FALSE;
	UINT32	imagesize, offset, rowsize, colors = 0;
	UINT	i, y;

	fh = file_open_rb(filename);
	if (fh == FILEH_INVALID) {
		return(NULL);
	}

	if ((file_read(fh, &bf, sizeof(bf)) != sizeof(bf)) ||
		(bf.bfType[0] != 'B') || (bf.bfType[1] != 'M')) {
		goto fr98_err;
	}
	// bmpdata_getinfo()で40バイトヘッダ、プレーン数、BI_RGBを確認
	if ((file_read(fh, &bi, sizeof(bi)) != sizeof(bi)) ||
		(bmpdata_getinfo(&bi, &bd) != SUCCESS) ||
		(bd.width != BMPWIDTH) ||
		((bd.height != BMPHEIGHT) && (bd.height != -BMPHEIGHT))) {
		goto fr98_err;
	}
	switch (bd.bpp) {
	case 1:
	case 4:
	case 8:
	case 16:
	case 24:
	case 32:
		break;
	default:
		goto fr98_err;
	}
	// 2048なので1ラインのパディングは気にしなくて良い
	rowsize = (UINT32)((BMPWIDTH * bd.bpp + 31) / 32 * 4);
	imagesize = LOADINTELDWORD(bi.biSizeImage);
	if (imagesize != 0 && imagesize != rowsize * BMPHEIGHT) {
		goto fr98_err;
	}
	offset = LOADINTELDWORD(bf.bfOffBits);
	if ((offset < sizeof(bf) + sizeof(bi)) ||
		(offset > 0x7fffffffUL - rowsize * BMPHEIGHT)) {
		goto fr98_err;
	}

	// 通常の1bpp FONT.BMPは従来通りパレットは確認しない
	// それ以外のパレット形式では色番号ではなくパレットの色で判定する
	if (bd.bpp <= 8 && (bd.bpp != 1 || overlay)) {
		colors = LOADINTELDWORD(bi.biClrUsed);
		if (colors == 0) {
			colors = 1U << bd.bpp;
		}
		if ((colors > (1U << bd.bpp)) || (bd.bpp == 1 && colors != 2) ||
			(offset < sizeof(bf) + sizeof(bi) + colors * 4) ||
			(file_read(fh, palette, colors * 4) != colors * 4)) {
			goto fr98_err;
		}
		if (bd.bpp == 1) {
			// 1bppの差分フォントは白と黒の2色のみ許可する
			if (palette[0] == 0 && palette[1] == 0 && palette[2] == 0 &&
				palette[4] == 255 && palette[5] == 255 && palette[6] == 255) {
				reverse = FALSE;
			}
			else if (palette[0] == 255 && palette[1] == 255 && palette[2] == 255 &&
				palette[4] == 0 && palette[5] == 0 && palette[6] == 0) {
				reverse = TRUE;
			}
			else {
				goto fr98_err;
			}
		}
		else {
			for (i=0; i<colors; i++) {
				mono[i] = pc98monobit(palette[i * 4 + 2],
						palette[i * 4 + 1], palette[i * 4]);
			}
		}
	}
	fptr = (long)offset;
	if (file_seek(fh, fptr, FSEEK_SET) != fptr) {
		goto fr98_err;
	}
	bmpdata = (UINT8 *)_MALLOC(BMPDATASIZE, "pc98font");
	if (bmpdata == NULL) {
		goto fr98_err;
	}

	if (bd.bpp == 1 && bd.height > 0) {
		// 下から上に格納されたモノクロBMPは従来通り一括読み込み
		if (file_read(fh, bmpdata, BMPDATASIZE) != BMPDATASIZE) {
			goto fr98_err;
		}
	}
	else {
		if (bd.bpp != 1) {
			row = (UINT8 *)_MALLOC(rowsize, "pc98fontrow");
			if (row == NULL) {
				goto fr98_err;
			}
		}
		for (y=0; y<BMPHEIGHT; y++) {
			// 従来の下から上に格納する1bpp形式に統一する
			dst = bmpdata + ((bd.height < 0 ? BMPHEIGHT - 1 - y : y) * BMPLINESIZE);
			if (bd.bpp == 1) {
				if (file_read(fh, dst, BMPLINESIZE) != BMPLINESIZE) {
					goto fr98_err;
				}
			}
			else if ((file_read(fh, row, rowsize) != rowsize) ||
					(pc98monoline(dst, row, bd.bpp, mono, colors) != SUCCESS)) {
				goto fr98_err;
			}
		}
	}
	if (reverse) {
		for (i=0; i<BMPDATASIZE; i++) {
			bmpdata[i] ^= 0xff;
		}
	}
	if (row != NULL) {
		_MFREE(row);
	}
	file_close(fh);
	return(bmpdata);

fr98_err:
	if (row != NULL) {
		_MFREE(row);
	}
	if (bmpdata != NULL) {
		_MFREE(bmpdata);
	}
	file_close(fh);
	return(NULL);
}

UINT8 fontpc98_read(const OEMCHAR *filename, UINT8 loading) {

	UINT8 *bmpdata;

	if (!(loading & FONTLOAD_16)) {
		return(loading);
	}
	bmpdata = pc98readbmp(filename, FALSE);
	if (bmpdata == NULL) {
		return(loading);
	}

	// 8x16 フォント(～0x7f)を読む必要がある？
	if (loading & FONT_ANK16a) {
		loading &= ~FONT_ANK16a;
		pc98ankcpy(fontrom + 0x80000, bmpdata, 0x000, 0x080, FALSE);
	}
	// 8x16 フォント(0x80～)を読む必要がある？
	if (loading & FONT_ANK16b) {
		loading &= ~FONT_ANK16b;
		pc98ankcpy(fontrom + 0x80800, bmpdata, 0x080, 0x100, FALSE);
	}

	// 第一水準漢字を読む必要がある？
	if (loading & FONT_KNJ1) {
		loading &= ~FONT_KNJ1;
		pc98knjcpy(fontrom, bmpdata, FONTKNJ1_START, FONTKNJ1_END, FALSE);
	}
	// 第二水準漢字を読む必要がある？
	if (loading & FONT_KNJ2) {
		loading &= ~FONT_KNJ2;
		pc98knjcpy(fontrom, bmpdata, FONTKNJ1_END, FONTKNJ2_END, FALSE);
	}
	// 拡張漢字を読む必要がある？
	if (loading & FONT_KNJ3) {
		loading &= ~FONT_KNJ3;
		pc98knjcpy(fontrom, bmpdata, FONTKNJ3_START, FONTKNJ3_END, FALSE);
	}

	_MFREE(bmpdata);
	return(loading);
}

// 差分適用の対象となる漢字コード範囲だけ元のフォントに戻す
static void pc98knjrestore(UINT8 *dst, const UINT8 *base, UINT from, UINT to) {

	UINT i, offset, size;

	offset = from << 4;
	size = (to - from) << 4;
	for (i=FONTKNJ_OVERLAY_PAGE; i<FONTKNJ_PAGE_END; i++) {
		CopyMemory(dst + (i << 12) + offset, base + (i << 12) + offset, size);
		CopyMemory(dst + (i << 12) + offset + 0x800,
				base + (i << 12) + offset + 0x800, size);
	}
}

void fontpc98_overlay(const OEMCHAR *filename, UINT8 *dst, const UINT8 *base) {

	UINT8 *bmpdata;

	// フォントを書き換える前にBMPの検査と読み込みを済ませる
	bmpdata = pc98readbmp(filename, TRUE);

	// FONT.BMPが扱うグリフ範囲だけ元のフォントに戻す
	// 外字（56h/57h）、リセット時にクリアされるページ、
	// BMP読み込み対象外の領域（8x8 ANK、キャラクタグラフィック）は変更しない
	pc98knjrestore(dst, base, FONTKNJ1_START, FONTKNJ2_END);
	pc98knjrestore(dst, base, FONTKNJ3_START, FONTKNJ3_END);
	CopyMemory(dst + 0x80000, base + 0x80000, 0x1000);

	if (bmpdata != NULL) {
		pc98ankcpy(dst + 0x80000, bmpdata, 0x000, 0x100, TRUE);
		pc98knjcpy(dst, bmpdata, FONTKNJ1_START, FONTKNJ2_END, TRUE);
		pc98knjcpy(dst, bmpdata, FONTKNJ3_START, FONTKNJ3_END, TRUE);
		_MFREE(bmpdata);
	}
}

