/*
 * OpenBOR - PVR Hardware Rendering
 */

#include <kos.h>
#include "types.h"
#include "video.h"
#include "vga.h"
#include "screen.h"
#include "openbor.h"
#include "filecache.h"
#include "pvr_render.h"

static int width=0, height=0, bpp=0, mode=0;
static int pvr_video_initialized = 0;

int video_set_mode(s_videomodes videomodes)
{
	dbglog(DBG_INFO, "[DC_VIDEO] video_set_mode called: %dx%d bpp=%d pvr_init=%d\n", videomodes.hRes, videomodes.vRes, videomodes.pixel, pvr_video_initialized);
	bpp = videomodes.pixel;
	width = videomodes.hRes;
	height = videomodes.vRes;
	if(videomodes.hRes==480) mode = 1;

    // Ignora chamadas com resolução inválida (ex: term_videomodes envia 0x0)
    if (width <= 0 || height <= 0) return 1;
	
    if (!pvr_video_initialized) {
        dbglog(DBG_INFO, "[DC_VIDEO] First init: vid_set_mode + pvr_render_init\n");
        vid_set_mode(DM_320x240, PM_RGB565);
        if (!pvr_render_init(width, height, bpp)) { dbglog(DBG_INFO, "[DC_VIDEO] pvr_render_init FAILED!\n"); return 0; }
        pvr_video_initialized = 1;
        dbglog(DBG_INFO, "[DC_VIDEO] First init DONE\n");
    } else {
        dbglog(DBG_INFO, "[DC_VIDEO] Subsequent call: clearing texture cache\n");
        pvr_texture_cache_clear();
        dbglog(DBG_INFO, "[DC_VIDEO] Cache cleared OK\n");
    }
	
	video_clearscreen();
	return 1;
}

extern unsigned char global_palette[768];

int video_copy_screen(s_screen* src)
{
	filecache_process();

    pvr_draw_screen(src, bpp, global_palette);
	
    pvr_render_end_frame();
    pvr_render_begin_frame();

	return 1;
}

void video_clearscreen()
{
	pvr_render_clear();
}

unsigned char global_palette[768];

void vga_setpalette(unsigned char* palette)
{
	if(bpp>1) return;
    memcpy(global_palette, palette, 768);
}

void video_fullscreen_flip()
{
}

void vga_vwait(void)
{
    // No-op seguro: sync é feito por pvr_wait_ready dentro de begin_frame
    thd_pass();
}
