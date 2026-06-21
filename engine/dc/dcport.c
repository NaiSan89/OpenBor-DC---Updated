/*
 * OpenBOR - http://www.LavaLit.com
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c) 2004 - 2011 OpenBOR Team
 */

/////////////////////////////////////////////////////////////////////////////

#include "dcport.h"
#include "timer.h"
#include "openbor.h"
#include "packfile.h"

/////////////////////////////////////////////////////////////////////////////

// OpenBOR uses its own GD-ROM reader and only needs KOS to provide IRQs, the
// ISO9660 mount used by /cd, and the Dreamcast controller driver.  Avoid the
// considerably broader INIT_DEFAULT set on retail/CD builds.
KOS_INIT_FLAGS(INIT_IRQ | INIT_CDROM | INIT_CONTROLLER);

/////////////////////////////////////////////////////////////////////////////

char packfile[128] = {"bor.pak"};
int cd_lba;

/////////////////////////////////////////////////////////////////////////////

unsigned readmsb32(const unsigned char *src)
{
	return
		((((unsigned)(src[0])) & 0xFF) << 24) |
		((((unsigned)(src[1])) & 0xFF) << 16) |
		((((unsigned)(src[2])) & 0xFF) <<  8) |
		((((unsigned)(src[3])) & 0xFF) <<  0);
}

/////////////////////////////////////////////////////////////////////////////

void borExit(int reset)
{
	arch_reboot();
}

/////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    printf("--> dcport main() started\n");
	setSystemRam();
	getRamStatus(BYTES);
	packfile_mode(0);
    printf("--> gdrom_init() chamando\n");
	if((cd_lba = gdrom_init()) <= 0)
	{
		printf("gdrom_init failed\n");
		arch_reboot();
	}
    printf("--> gdrom_init() sucesso, lba: %d\n", cd_lba);
	openborMain(argc, argv);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////
