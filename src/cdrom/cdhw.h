/****************************************************************************
* Neo.CD Respin 1.2 - experimental native BIOS CD hardware path
****************************************************************************/
#ifndef __NEOCD_CDHARDWARE__
#define __NEOCD_CDHARDWARE__

#include <gccore.h>

void cdhw_reset(void);
int  cdhw_handles_byte(unsigned int address);
u8   cdhw_read8(unsigned int address);
void cdhw_write8(unsigned int address, u8 value);

#endif
