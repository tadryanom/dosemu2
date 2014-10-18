/*
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */

/* definitions for rendering graphics modes -- the middle layer
   between the graphics frontends and the remapper */

struct render_system
{
  void (*refresh_rect)(int x, int y, unsigned width, unsigned height);
};

int register_render_system(struct render_system *render_system);
int remapper_init(unsigned *image_mode, unsigned bits_per_pixel,
		  int have_true_color, int have_shmap);
void remapper_done(void);
void get_mode_parameters(int *wx_res, int *wy_res, int ximage_mode,
			 vga_emu_update_type *veut);
int update_screen(vga_emu_update_type *veut);
void render_init(uint8_t *img, ColorSpaceDesc *csd, int width, int height,
	int scan_len);
