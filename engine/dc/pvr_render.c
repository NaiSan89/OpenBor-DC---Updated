#include <kos.h>
#include <malloc.h>
#include <string.h>
#include "pvr_render.h"

// Define a estrutura para o cache de texturas
#define MAX_CACHED_TEXTURES 256

typedef struct {
    s_sprite *sprite_ptr; // Chave de busca (o ponteiro original do OpenBOR)
    void *palette_ptr;    // Chave de busca 2 (remap table)
    pvr_ptr_t vram_ptr;   // Ponteiro na memória de vídeo
    int p2_w;             // Largura em potência de 2
    int p2_h;             // Altura em potência de 2
    int last_used_frame;  // Para algoritmo LRU de limpeza
} pvr_texture_cache_t;

static pvr_texture_cache_t tex_cache[MAX_CACHED_TEXTURES];
static int current_frame = 0;
static float current_sprite_z = 0.001f;
static int pvr_initialized = 0;
static int frame_in_progress = 0;
static pvr_ptr_t bg_vram = NULL;

// Calcula a próxima potência de 2
static int next_power_of_2(int val) {
    int power = 8;
    while (power < val) power *= 2;
    return power;
}

int pvr_render_init(int w, int h, int bpp) {
    pvr_init_params_t params = {
        { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
        512 * 1024 // 512KB vertex buffer (padrão KOS)
    };
    
    pvr_init(&params);
    memset(tex_cache, 0, sizeof(tex_cache));

    bg_vram = pvr_mem_malloc(512 * 256 * 2); // 256KB para 320x240
    dbglog(DBG_INFO, "[DC_PVR] pvr_render_init: bg_vram alocado = %p\n", bg_vram);
    
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);
    current_sprite_z = 0.001f;
    frame_in_progress = 1;
    pvr_initialized = 1;
    return 1;
}

void pvr_render_begin_frame(void) {
    if (frame_in_progress) return; // Evita abrir dois frames seguidos
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);
    current_sprite_z = 0.001f;
    frame_in_progress = 1;
}

void pvr_render_end_frame(void) {
    if (!frame_in_progress) return; // Evita fechar frame já fechado
    
    // BUGFIX PVR: Submeter um polígono vazio trava a placa de vídeo.
    // Garantimos que sempre haja pelo menos 1 polígono processado.
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));
    
    vert.flags = PVR_CMD_VERTEX;
    vert.x = 0.0f; vert.y = 0.0f; vert.z = 0.0001f;
    vert.u = 0.0f; vert.v = 0.0f;
    vert.argb = 0x00000000; // Totalmente transparente
    vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));
    
    vert.x = 1.0f; vert.y = 0.0f;
    pvr_prim(&vert, sizeof(vert));
    
    vert.x = 0.0f; vert.y = 1.0f;
    pvr_prim(&vert, sizeof(vert));
    
    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = 1.0f; vert.y = 1.0f;
    pvr_prim(&vert, sizeof(vert));
    
    pvr_list_finish();
    pvr_scene_finish();
    current_frame++;
    frame_in_progress = 0;
}

void pvr_render_clear(void) {
    // PVR limpa o background automaticamente com a cor configurada (pvr_set_bg_color)
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);
}

void pvr_texture_cache_clear(void) {
    int i;
    for (i = 0; i < MAX_CACHED_TEXTURES; i++) {
        if (tex_cache[i].vram_ptr != NULL) {
            pvr_mem_free(tex_cache[i].vram_ptr);
            tex_cache[i].vram_ptr = NULL;
            tex_cache[i].sprite_ptr = NULL;
            tex_cache[i].palette_ptr = NULL;
        }
    }
}

// Encontra ou faz o upload de uma textura para a VRAM
static pvr_texture_cache_t* get_or_upload_texture(s_sprite *sprite, void *pal_ptr) {
    if (!sprite) return NULL;

    int oldest_idx = 0;
    int oldest_frame = current_frame;
    int free_idx = -1;
    int i;

    // Busca no cache
    for (i = 0; i < MAX_CACHED_TEXTURES; i++) {
        if (tex_cache[i].sprite_ptr == sprite && tex_cache[i].palette_ptr == pal_ptr) {
            tex_cache[i].last_used_frame = current_frame;
            return &tex_cache[i];
        }
        if (tex_cache[i].vram_ptr == NULL && free_idx == -1) {
            free_idx = i;
        }
        if (tex_cache[i].vram_ptr != NULL && tex_cache[i].last_used_frame < oldest_frame) {
            oldest_frame = tex_cache[i].last_used_frame;
            oldest_idx = i;
        }
    }

    // Se não encontrou, precisa fazer upload. Qual slot usar?
    int slot = free_idx;
    if (slot == -1) {
        // Cache cheio, substitui o mais antigo (LRU)
        slot = oldest_idx;
        if (tex_cache[slot].vram_ptr) {
            pvr_mem_free(tex_cache[slot].vram_ptr);
        }
    }

    // Pega as dimensões reais e a potência de 2
    int w = sprite->width;
    int h = sprite->height;
    int p2w = next_power_of_2(w);
    int p2h = next_power_of_2(h);

    // Aloca VRAM (formato ARGB4444 para facilitar mistura de paletas e alpha)
    pvr_ptr_t vram = pvr_mem_malloc(p2w * p2h * 2); 
    while (!vram) {
        int oldest_v_idx = -1;
        // Só permite ejetar texturas que não foram usadas nos últimos 2 frames
        // (Devido ao pipeline do PVR, o frame 'current_frame-1' pode estar sendo renderizado agora)
        int oldest_v_frame = current_frame - 1; 
        int j;
        for (j = 0; j < MAX_CACHED_TEXTURES; j++) {
            if (tex_cache[j].vram_ptr && tex_cache[j].last_used_frame < oldest_v_frame) {
                oldest_v_frame = tex_cache[j].last_used_frame;
                oldest_v_idx = j;
            }
        }
        
        if (oldest_v_idx == -1) {
            // Se chegamos aqui, todas as texturas estão em uso neste frame (ou no anterior).
            // Não podemos liberar a VRAM em uso ou o PVR vai travar/corromper.
            break; 
        }
        
        pvr_mem_free(tex_cache[oldest_v_idx].vram_ptr);
        tex_cache[oldest_v_idx].vram_ptr = NULL;
        tex_cache[oldest_v_idx].sprite_ptr = NULL;
        tex_cache[oldest_v_idx].palette_ptr = NULL;
        vram = pvr_mem_malloc(p2w * p2h * 2);
    }
    
    if (!vram) {
        // VRAM esgotada por este frame específico.
        // Oculta o sprite para evitar travamento da placa de vídeo.
        return NULL; 
    }

    // Converte os pixels indexados (8-bit) para ARGB4444 (16-bit) na memória do sistema
    unsigned short *temp_buf = (unsigned short *)memalign(32, p2w * p2h * 2);
    memset(temp_buf, 0, p2w * p2h * 2); // Limpa com transparente

    extern unsigned char global_palette[768];
    unsigned short *palette16 = (unsigned short *)pal_ptr;

    int *linetab = (int *)(sprite->data);
    int y;
    
    int max_y = h > p2h ? p2h : h;

    // OpenBOR sprites are RLE encoded (PIXEL_x8)
    for (y = 0; y < max_y; y++) {
        int lx = 0;
        unsigned char *data = ((unsigned char *)linetab) + (*linetab);
        linetab++;
        
        while (lx < w) {
            int trans_count = *data++;
            if (trans_count == 0xFF) break;
            lx += trans_count;
            if (lx >= w) break;

            int vis_count = *data++;
            if (!vis_count) continue;
            
            for (; vis_count > 0; vis_count--) {
                unsigned char idx = *data++;
                if (lx >= w || lx >= p2w) { lx++; continue; }
                
                unsigned short p;
                if (palette16) {
                    p = palette16[idx];
                } else {
                    int po = idx * 3;
                    p = ((global_palette[po] >> 3) << 11) | ((global_palette[po+1] >> 2) << 5) | (global_palette[po+2] >> 3);
                }
                
                // Convert RGB565 to ARGB4444
                unsigned char r = (p >> 11) & 0x1F;
                unsigned char g = (p >> 5) & 0x3F;
                unsigned char b = p & 0x1F;
                temp_buf[y * p2w + lx] = 0xF000 | ((r >> 1) << 8) | ((g >> 2) << 4) | (b >> 1);
                lx++;
            }
        }
    }

    // Copia para a VRAM
    pvr_txr_load_ex(temp_buf, vram, p2w, p2h, PVR_TXRLOAD_16BPP);
    free(temp_buf);

    tex_cache[slot].sprite_ptr = sprite;
    tex_cache[slot].palette_ptr = pal_ptr;
    tex_cache[slot].vram_ptr = vram;
    tex_cache[slot].p2_w = p2w;
    tex_cache[slot].p2_h = p2h;
    tex_cache[slot].last_used_frame = current_frame;

    return &tex_cache[slot];
}

void pvr_draw_sprite(int x, int y, s_sprite *sprite, s_drawmethod *drawmethod) {
    if (!sprite || !frame_in_progress) return;
    if (sprite->width <= 0 || sprite->height <= 0) return;
    
    void *pal = drawmethod && drawmethod->table ? drawmethod->table : sprite->palette;
    pvr_texture_cache_t *cached = get_or_upload_texture(sprite, pal);
    if (!cached) return;

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    // Configura contexto PVR para textura Translúcida ARGB4444
    // pvr_txr_load_ex converte texturas para TWIDDLED automaticamente, então não usamos NONTWIDDLED.
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444, cached->p2_w, cached->p2_h, cached->vram_ptr, PVR_FILTER_NEAREST);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    int flipx = drawmethod ? drawmethod->flipx : 0;
    int flipy = drawmethod ? drawmethod->flipy : 0;

    if (flipx) {
        x += sprite->centerx;
    } else {
        x -= sprite->centerx;
    }
    y -= sprite->centery;

    // Coordenadas reais
    float x1 = x;
    float y1 = y;
    float x2 = x + sprite->width;
    float y2 = y + sprite->height;
    
    // Z-Order: Usamos a ordem de submissão do motor. OpenBOR sorteia perfeitamente (Sombras -> Personagens).
    // Como MAIOR Z fica na frente no Dreamcast, apenas incrementamos o Z a cada sprite!
    float z = current_sprite_z;
    current_sprite_z += 0.001f;

    float u1 = 0.0f;
    float v1 = 0.0f;
    float u2 = (float)sprite->width / cached->p2_w;
    float v2 = (float)sprite->height / cached->p2_h;

    if (flipx) {
        float temp = u1; u1 = u2; u2 = temp;
    }
    if (flipy) {
        float temp = v1; v1 = v2; v2 = temp;
    }

    // TODO: Adicionar suporte para drawmethod->flipx, flipy, remaps e alpha blending extra aqui.

    // Vertex 1 (Top-Left)
    vert.flags = PVR_CMD_VERTEX;
    vert.x = x1; vert.y = y1; vert.z = z;
    vert.u = u1; vert.v = v1;
    vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    // Vertex 2 (Top-Right)
    vert.x = x2; vert.y = y1;
    vert.u = u2; vert.v = v1;
    pvr_prim(&vert, sizeof(vert));

    // Vertex 3 (Bottom-Left)
    vert.x = x1; vert.y = y2;
    vert.u = u1; vert.v = v2;
    pvr_prim(&vert, sizeof(vert));

    // Vertex 4 (Bottom-Right)
    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x2; vert.y = y2;
    vert.u = u2; vert.v = v2;
    pvr_prim(&vert, sizeof(vert));
}

void pvr_draw_screen(s_screen *src, int bpp, unsigned char *global_palette) {
    if (!src || !frame_in_progress || !bg_vram) {
        dbglog(DBG_INFO, "[DC_PVR] pvr_draw_screen aborted: src=%p, frame=%d, vram=%p\n", src, frame_in_progress, bg_vram);
        return;
    }

    int w = src->width;
    int h = src->height;
    if (w <= 0 || h <= 0) {
        dbglog(DBG_INFO, "[DC_PVR] pvr_draw_screen invalid size: %dx%d\n", w, h);
        return;
    }
    
    // Limitar ao tamanho do bg_vram alocado (512x256)
    if (w > 512) { dbglog(DBG_INFO, "[DC_PVR] width > 512! (%d)\n", w); w = 512; }
    if (h > 256) { dbglog(DBG_INFO, "[DC_PVR] height > 256! (%d)\n", h); h = 256; }
    int p2w = next_power_of_2(w);
    int p2h = next_power_of_2(h);

    if (!bg_vram) return;

    unsigned short *temp_buf = (unsigned short *)memalign(32, p2w * p2h * 2);
    if (!temp_buf) return;
    memset(temp_buf, 0, p2w * p2h * 2);
    
    int x, y;

    if (bpp == 1) {
        unsigned char *pixels = (unsigned char *)src->data;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                unsigned char idx = pixels[y * w + x];
                int pal_offset = idx * 3;
                unsigned char r = global_palette[pal_offset];
                unsigned char g = global_palette[pal_offset + 1];
                unsigned char b = global_palette[pal_offset + 2];
                temp_buf[y * p2w + x] = 0xF000 | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            }
        }
    } else { // Assume 16bpp (RGB565 -> ARGB1555 / ARGB4444)
        unsigned short *pixels = (unsigned short *)src->data;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                unsigned short p = pixels[y * w + x];
                unsigned char r = (p >> 11) & 0x1F;
                unsigned char g = (p >> 5) & 0x3F;
                unsigned char b = p & 0x1F;
                temp_buf[y * p2w + x] = 0xF000 | ((r >> 1) << 8) | ((g >> 2) << 4) | (b >> 1);
            }
        }
    }

    pvr_txr_load_ex(temp_buf, bg_vram, p2w, p2h, PVR_TXRLOAD_16BPP);
    free(temp_buf);

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444, p2w, p2h, bg_vram, PVR_FILTER_NEAREST);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    float u2 = (float)w / p2w;
    float v2 = (float)h / p2h;
    float z = 0.0001f; // Background (fundo) - Menor valor Z para ficar atrás de tudo

    vert.flags = PVR_CMD_VERTEX;
    vert.x = 0.0f; vert.y = 0.0f; vert.z = z;
    vert.u = 0.0f; vert.v = 0.0f;
    vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = w; vert.y = 0.0f;
    vert.u = u2; vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 0.0f; vert.y = h;
    vert.u = 0.0f; vert.v = v2;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = w; vert.y = h;
    vert.u = u2; vert.v = v2;
    pvr_prim(&vert, sizeof(vert));
}
