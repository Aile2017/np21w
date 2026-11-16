#ifdef __cplusplus
extern "C" {
#endif

REG8 joymng_getstat(void);
REG8 joymng_available(void);
UINT32 joymng_getAnalogX(void);
UINT32 joymng_getAnalogY(void);
#if defined(SUPPORT_NPJOY)
BOOL joymng_getnpjoy(UINT16 *x, UINT16 *y, UINT8 *buttons);
#endif

#ifdef __cplusplus
}
#endif


// ----

void joymng_initialize(void);
void joymng_sync(void);

