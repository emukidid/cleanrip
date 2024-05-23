/**
 * CleanRip - FrameBufferMagic.h
 * Copyright (C) 2010 emu_kidid
 *
 * CleanRip homepage: http://code.google.com/p/cleanrip/
 * email address: emukidid@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 **/

#ifndef FRAMEBUFFERMAGIC_H_
#define FRAMEBUFFERMAGIC_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>

#define D_WARN  0
#define D_INFO  1
#define D_FAIL  2
#define D_PASS  3
#define B_NOSELECT 0
#define B_SELECTED 1

#define BUTTON_COLOUR_INNER COLOR_BLUE
#define BUTTON_COLOUR_OUTER COLOR_SILVER

#define PROGRESS_BOX_WIDTH  572
#define PROGRESS_BOX_HEIGHT 170
#define PROGRESS_BOX_BAR    COLOR_GREEN
#define PROGRESS_BOX_BARALL COLOR_RED
#define PROGRESS_BOX_BACK   COLOR_BLUE

enum TextureId
{
	TEX_BACKDROP=0,
	TEX_BTNA,
	TEX_BTNB
};

void fbm_initialise(void);
void fbm_frame_start(void);
void fbm_frame_finish(void);
void fbm_draw_box(int x1, int y1, int x2, int y2);
void fbm_draw_msg_box(int type, char *message);
void fbm_draw_raw_font(int x, int y, char *message);
void fbm_draw_A_button(int x, int y);
void fbm_draw_B_button(int x, int y);
int fbm_draw_yes_no_dialog(char *line1, char *line2);
void fbm_draw_selection_button(int x1, int y1, int x2, int y2, char *message, int mode, u32 color);
void fbm_draw_progress_bar(int percent, char *message);
#endif
