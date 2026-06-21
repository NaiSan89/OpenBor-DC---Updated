#ifndef _PVR_RENDER_H_
#define _PVR_RENDER_H_

#include <dc/pvr.h>
#include "types.h"

// Inicializa o sistema gráfico PVR
int pvr_render_init(int w, int h, int bpp);

// Inicia um quadro (chamado no início de video_copy_screen)
void pvr_render_begin_frame(void);

// Finaliza um quadro e inverte o buffer
void pvr_render_end_frame(void);

// Limpa a tela com uma cor de fundo
void pvr_render_clear(void);

// Desenha a tela base do software (fundo e interface)
void pvr_draw_screen(s_screen *src, int bpp, unsigned char *global_palette);

// Função para desenhar um sprite do OpenBOR através do PVR
void pvr_draw_sprite(int x, int y, s_sprite *sprite, s_drawmethod *drawmethod);

// Limpa todo o cache de texturas VRAM
void pvr_texture_cache_clear(void);

#endif // _PVR_RENDER_H_
