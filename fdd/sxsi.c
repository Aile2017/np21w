#include	"compiler.h"
#include	"strres.h"
#include	"dosio.h"
#include	"sysmng.h"
#include	"cpucore.h"
#include	"pccore.h"
#include	"iocore.h"
#include	"sxsi.h"
#if defined(SUPPORT_IDEIO)
#include	"ideio.h"
#include	"atapicmd.h"
#endif
#if defined(SUPPORT_MULTITHREAD)
#include	"np2mt.h"
#endif

	_SXSIDEV	sxsi_dev[SASIHDD_MAX + SCSIHDD_MAX];


// ----

static BRESULT nc_reopen(SXSIDEV sxsi) {

	(void)sxsi;
	return(FAILURE);
}

static REG8	nc_read(SXSIDEV sxsi, FILEPOS pos, UINT8 *buf, UINT size) {

	(void)sxsi;
	(void)pos;
	(void)buf;
	(void)size;
	return(0x60);
}

static REG8 nc_write(SXSIDEV sxsi, FILEPOS pos, const UINT8 *buf, UINT size) {

	(void)sxsi;
	(void)pos;
	(void)buf;
	(void)size;
	return(0x60);
}

static REG8 nc_format(SXSIDEV sxsi, FILEPOS pos) {

	(void)sxsi;
	(void)pos;
	return(0x60);
}

static void nc_close(SXSIDEV sxsi) {

	(void)sxsi;
}

static void nc_destroy(SXSIDEV sxsi) {

	(void)sxsi;
}


static void sxsi_disconnect(SXSIDEV sxsi) {

	if (sxsi) {
#if defined(SUPPORT_IDEIO)
		if ((sxsi->flag & SXSIFLAG_READY) && (sxsi->devtype == SXSIDEV_CDROM)) {
#if defined(_WINDOWS)
			// 非同期読み出し中なら待つ
			atapi_dataread_asyncwait(INFINITE);
#endif
			ideio_notify(sxsi->drv, 0);
		}
#endif
		if (sxsi->flag & SXSIFLAG_FILEOPENED) {
			(*sxsi->close)(sxsi);
		}
		if (sxsi->flag & SXSIFLAG_READY) {
			(*sxsi->destroy)(sxsi);
		}
		sxsi->flag = 0;
		sxsi->reopen = nc_reopen;
		sxsi->read = nc_read;
		sxsi->write = nc_write;
		sxsi->format = nc_format;
		sxsi->close = nc_close;
		sxsi->destroy = nc_destroy;
	}
}


// ----

void sxsi_initialize(void) {

	UINT	i;

	ZeroMemory(sxsi_dev, sizeof(sxsi_dev));
	for (i=0; i<SASIHDD_MAX; i++) {
		sxsi_dev[i].drv = (UINT8)(SXSIDRV_SASI + i);
	}
#if defined(SUPPORT_SCSI)
	for (i=0; i<SCSIHDD_MAX; i++) {
		sxsi_dev[SASIHDD_MAX + i].drv = (UINT8)(SXSIDRV_SCSI + i);
	}
#endif
	for (i=0; i<NELEMENTS(sxsi_dev); i++) {
		sxsi_disconnect(sxsi_dev + i);
	}
}

void sxsi_allflash(void) {

	SXSIDEV	sxsi;
	SXSIDEV	sxsiterm;

	sxsi = sxsi_dev;
	sxsiterm = sxsi + NELEMENTS(sxsi_dev);
	while(sxsi < sxsiterm) {
		if (sxsi->flag & SXSIFLAG_FILEOPENED) {
			sxsi->flag &= ~SXSIFLAG_FILEOPENED;
			(*sxsi->close)(sxsi);
		}
		sxsi++;
	}
}

void sxsi_alltrash(void) {

	SXSIDEV	sxsi;
	SXSIDEV	sxsiterm;

	sxsi = sxsi_dev;
	sxsiterm = sxsi + NELEMENTS(sxsi_dev);
	while(sxsi < sxsiterm) {
		sxsi_disconnect(sxsi);
		sxsi++;
	}
}

BOOL sxsi_isconnect(SXSIDEV sxsi) {

	if (sxsi) {
		switch(sxsi->devtype) {
			case SXSIDEV_HDD:
				if (sxsi->flag & SXSIFLAG_READY) {
					return(TRUE);
				}
				break;

			case SXSIDEV_CDROM:
				return(TRUE);
		}
	}
	return(FALSE);
}

BRESULT sxsi_prepare(SXSIDEV sxsi) {

	if ((sxsi == NULL) || (!(sxsi->flag & SXSIFLAG_READY))) {
		return(FAILURE);
	}
	if (!(sxsi->flag & SXSIFLAG_FILEOPENED)) {
		if ((*sxsi->reopen)(sxsi) == SUCCESS) {
			sxsi->flag |= SXSIFLAG_FILEOPENED;
		}
		else {
			return(FAILURE);
		}
	}
	sysmng_hddaccess(sxsi->drv);
	return(SUCCESS);
}


// ----

SXSIDEV sxsi_getptr(REG8 drv) {

	UINT	num;

	num = drv & 0x0f;
	if (!(drv & 0x20)) {					// SASI or IDE
		if (num < SASIHDD_MAX) {
			return(sxsi_dev + num);
		}
	}
#if defined(SUPPORT_SCSI)
	else {
		if (num < SCSIHDD_MAX) {			// SCSI
			return(sxsi_dev + SASIHDD_MAX + num);
		}
	}
#endif
	return(NULL);
}

OEMCHAR *sxsi_getfilename(REG8 drv) {

	SXSIDEV	sxsi;

	sxsi = sxsi_getptr(drv);
	if ((sxsi) && (sxsi->flag & SXSIFLAG_READY)) {
		return(sxsi->fname);
	}
	return(NULL);
}

BRESULT sxsi_setdevtype(REG8 drv, UINT8 dev) {

	SXSIDEV	sxsi;

	sxsi = sxsi_getptr(drv);
	if (sxsi) {
		if (sxsi->devtype != dev) {
			sxsi_disconnect(sxsi);
			sxsi->devtype = dev;
		}
		return(SUCCESS);
	}
	else {
		return(FAILURE);
	}
}

UINT8 sxsi_getdevtype(REG8 drv) {

	SXSIDEV	sxsi;

	sxsi = sxsi_getptr(drv);
	if (sxsi) {
		return(sxsi->devtype);
	}
	else {
		return(SXSIDEV_NC);
	}
}

#ifdef SUPPORT_NVL_IMAGES
BRESULT sxsihdd_nvl_open(SXSIDEV sxsi, const OEMCHAR *fname);
#endif

BRESULT sxsi_devopen(REG8 drv, const OEMCHAR *fname) {

	SXSIDEV		sxsi;
	BRESULT		r;

	sxsi = sxsi_getptr(drv);
	if (sxsi == NULL) {
		goto sxsiope_err;
	}
	switch(sxsi->devtype) {
		case SXSIDEV_HDD:
			if ((fname == NULL) || (fname[0] == '\0')) {
				goto sxsiope_err;
			}
			r = sxsihdd_open(sxsi, fname);
#ifdef SUPPORT_NVL_IMAGES
			if (r == FAILURE)
			{
				r = sxsihdd_nvl_open(sxsi, fname);
			}
#endif
			break;

		case SXSIDEV_CDROM:
#if defined(SUPPORT_IDEIO)
			{
				OEMCHAR path[MAX_PATH];
				int num = drv & 0x03;

				// fname退避
				file_cpyname(path, fname ? fname : OEMTEXT(""), NELEMENTS(path));
				sxsi_disconnect(sxsi);
				sxsi->fname[0] = '\0';
				np2cfg.idecd[num][0] = '\0';
				if (!path[0]) {
					sysmng_updatecaption(SYS_UPDATECAPTION_FDD);
					return(SUCCESS);
				}

				r = sxsicd_open(sxsi, path);
				if (r == SUCCESS) {
					file_cpyname(sxsi->fname, path, NELEMENTS(sxsi->fname));
					sxsi->flag = SXSIFLAG_READY | SXSIFLAG_FILEOPENED;
					ideio_notify(sxsi->drv, 1);
				}
#ifdef SUPPORT_PHYSICAL_CDDRV
				// 実ドライブは空でも選択状態を維持
				if (r == SUCCESS || _tcsnicmp(path, OEMTEXT("\\\\.\\"), 4) == 0) {
#else
				if (r == SUCCESS) {
#endif
					file_cpyname(np2cfg.idecd[num], path, NELEMENTS(np2cfg.idecd[num]));
				}
				sysmng_updatecaption(SYS_UPDATECAPTION_FDD);
				return(r);
			}
#else
			r = FAILURE;
#endif
			break;

		default:
			r = FAILURE;
			break;
	}
	if (r != SUCCESS) {
		goto sxsiope_err;
	}
	file_cpyname(sxsi->fname, fname, NELEMENTS(sxsi->fname));
	sxsi->flag = SXSIFLAG_READY | SXSIFLAG_FILEOPENED;
#if defined(SUPPORT_IDEIO)
	ideio_notify(sxsi->drv, 1);
#endif
	return(SUCCESS);

sxsiope_err:
	return(FAILURE);
}

void sxsi_devclose(REG8 drv) {

	SXSIDEV		sxsi;

	sxsi = sxsi_getptr(drv);
	sxsi_disconnect(sxsi);
}

BOOL sxsi_issasi(void) {

	REG8	drv;
	SXSIDEV	sxsi;
	BOOL	ret;

	ret = FALSE;
	for (drv=0x00; drv<0x04; drv++) {
		sxsi = sxsi_getptr(drv);
		if (sxsi) {
			if ((drv < 0x02) && (sxsi->devtype == SXSIDEV_HDD)) {
				if (sxsi->flag & SXSIFLAG_READY) {
					if (sxsi->mediatype & SXSIMEDIA_INVSASI) {
						return(FALSE);
					}
					ret = TRUE;
				}
			}
			else {
				return(FALSE);
			}
		}
	}
	return(ret);
}

BOOL sxsi_isscsi(void) {

	REG8	drv;
	SXSIDEV	sxsi;

	for (drv=0x20; drv<0x28; drv++) {
		sxsi = sxsi_getptr(drv);
		if (sxsi_isconnect(sxsi)) {
			return(TRUE);
		}
	}
	return(FALSE);
}

BOOL sxsi_iside(void) {

	REG8	drv;
	SXSIDEV	sxsi;

	for (drv=0x00; drv<0x04; drv++) {
		sxsi = sxsi_getptr(drv);
		if (sxsi_isconnect(sxsi)) {
			return(TRUE);
		}
	}
	return(FALSE);
}



REG8 sxsi_read(REG8 drv, FILEPOS pos, UINT8 *buf, UINT size) {

	SXSIDEV	sxsi;

	sxsi = sxsi_getptr(drv);
	if (sxsi != NULL) {
		return(sxsi->read(sxsi, pos, buf, size));
	}
	else {
		return(0x60);
	}
}

REG8 sxsi_write(REG8 drv, FILEPOS pos, const UINT8 *buf, UINT size) {

	SXSIDEV	sxsi;

	sxsi = sxsi_getptr(drv);
	if (sxsi != NULL) {
		return(sxsi->write(sxsi, pos, buf, size));
	}
	else {
		return(0x60);
	}
}

REG8 sxsi_format(REG8 drv, FILEPOS pos) {

	SXSIDEV	sxsi;

	sxsi = sxsi_getptr(drv);
	if (sxsi != NULL) {
		return(sxsi->format(sxsi, pos));
	}
	else {
		return(0x60);
	}
}

