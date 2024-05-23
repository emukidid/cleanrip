/**
 * CleanRip - IPLFontWrite.h
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

#ifndef IPLFontWrite_H
#define IPLFontWrite_H

#include "FrameBufferMagic.h"

#define back_framewidth vmode->fbWidth
#define back_frameheight vmode->xfbHeight

extern GXColor DEFAULT_COLOR;
extern GXColor DISABLED_COLOR;
extern char txtbuffer[2048];

void font_initialise(void);
void font_write(int x, int y, char *string);
void font_write_styled(int x, int y, char *string, float size, bool centered, GXColor color);
int font_text_size_in_pixels(char *string);
float font_text_scale_to_fit_width(char *string, int width);
void font_write_center(int y, char *string);

#endif
