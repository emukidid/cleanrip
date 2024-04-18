/**
 * CleanRip - IPLFontWrite.c
 * Copyright (C) 2010 emu_kidid
 *
 * Font blitter for IPL fonts
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

#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <ogcsys.h>
#include <string.h>
#include "IPLFontWrite.h"
#include "main.h"

char txtbuffer[2048];
extern void __SYS_ReadROM(void *buf, u32 len, u32 offset);

#define FONT_TEX_SIZE_I4 ((512*512)>>1)
#define FONT_SIZE_ANSI (288 + 131072)
#define STR_HEIGHT_OFFSET 0

typedef struct {
	u16 s[256], t[256], font_size[256], fheight;
} CHAR_INFO;

static unsigned char font_texture_buffer[ 0x40000 ] __attribute__((aligned(32)));

CHAR_INFO font_chars;
GXTexObj font_tex_obj;

GXColor DEFAULT_COLOR = (GXColor) {255,255,255,255};
GXColor DISABLED_COLOR = (GXColor) {175,175,182,255};
GXColor FONT_COLOR = (GXColor) {255,255,255,255};


void decode_Yay0(unsigned char *s, unsigned char *d)
{
	int i, j, k, p, q, cnt;

	i = *(unsigned long *)(s + 4);	  // size of decoded data
	j = *(unsigned long *)(s + 8);	  // link table
	k = *(unsigned long *)(s + 12);	 // byte chunks and count modifiers

	q = 0;					// current offset in dest buffer
	cnt = 0;				// mask bit counter
	p = 16;					// current offset in mask table

	unsigned long r22 = 0, r5;
	
	do
	{
		// if all bits are done, get next mask
		if(cnt == 0)
		{
			// read word from mask data block
			r22 = *(unsigned long *)(s + p);
			p += 4;
			cnt = 32;   // bit counter
		}
		// if next bit is set, chunk is non-linked
		if(r22 & 0x80000000)
		{
			// get next byte
			*(unsigned char *)(d + q) = *(unsigned char *)(s + k);
			k++, q++;
		}
		// do copy, otherwise
		else
		{
			// read 16-bit from link table
			int r26 = *(unsigned short *)(s + j);
			j += 2;
			// 'offset'
			int r25 = q - (r26 & 0xfff);
			// 'count'
			int r30 = r26 >> 12;
			if(r30 == 0)
			{
				// get 'count' modifier
				r5 = *(unsigned char *)(s + k);
				k++;
				r30 = r5 + 18;
			}
			else r30 += 2;
			// do block copy
			unsigned char *pt = ((unsigned char*)d) + r25;
			int i;
			for(i=0; i<r30; i++)
			{
				*(unsigned char *)(d + q) = *(unsigned char *)(pt - 1);
				q++, pt++;
			}
		}
		// next bit in mask
		r22 <<= 1;
		cnt--;

	} while(q < i);
}

void convert_I2_to_I4(void *dst, void *src, int xres, int yres)
{
	// I4 has 8x8 tiles
	int x, y;
	unsigned char *d = (unsigned char*)dst;
	unsigned char *s = (unsigned char*)src;

	for (y = 0; y < yres; y += 8)
		for (x = 0; x < xres; x += 8)
		{
			int iy, ix;
			for (iy = 0; iy < 8; ++iy, s+=2)
			{
				for (ix = 0; ix < 2; ++ix)
				{
					int v = s[ix];
					*d++ = (((v>>6)&3)<<6) | (((v>>6)&3)<<4) | (((v>>4)&3)<<2) | ((v>>4)&3);
					*d++ = (((v>>2)&3)<<6) | (((v>>2)&3)<<4) | (((v)&3)<<2) | ((v)&3);
				}
			}
		}
}

void font_initialise(void)
{
	void* font_area = memalign(32,FONT_SIZE_ANSI);
	memset(font_area,0,FONT_SIZE_ANSI);
	void* packed_data = (void*)(((u32)font_area+119072)&~31);
	void* unpacked_data = (void*)((u32)font_area+288);
	__SYS_ReadROM(packed_data,0x3000,0x1FCF00);
	decode_Yay0(packed_data,unpacked_data);

	sys_fontheader *font_data = (sys_fontheader*)unpacked_data;

	convert_I2_to_I4((void*)font_texture_buffer, (void*)((u32)unpacked_data+font_data->sheet_image), font_data->sheet_width, font_data->sheet_height);
	DCFlushRange(font_texture_buffer, FONT_TEX_SIZE_I4);

	int i;
	for (i=0; i<256; ++i)
	{
		int c = i;

		if ((c < font_data->first_char) || (c > font_data->last_char)) c = font_data->inval_char;
		else c -= font_data->first_char;

		font_chars.font_size[i] = ((unsigned char*)font_data)[font_data->width_table + c];

		int r = c / font_data->sheet_column;
		c %= font_data->sheet_column;

		font_chars.s[i] = c * font_data->cell_width;
		font_chars.t[i] = r * font_data->cell_height;
	}
	
	font_chars.fheight = font_data->cell_height;

	free(font_area);
}

void draw_font_initialise(GXColor FONT_COLOR)
{
	Mtx44 GXprojection2D;
	Mtx GXmodelView2D;

	// Reset various parameters from gfx plugin
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	guMtxIdentity(GXmodelView2D);
	GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	guOrtho(GXprojection2D, 0, 479, 0, 639, 0, 700);
	GX_LoadProjectionMtx(GXprojection2D, GX_ORTHOGRAPHIC);

	GX_SetZMode(GX_DISABLE,GX_ALWAYS,GX_TRUE);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	//set vertex attribute formats here
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	//enable textures
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_InvalidateTexAll();
	GX_InitTexObj(&font_tex_obj, &font_texture_buffer[0], 512, 512, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_LoadTexObj(&font_tex_obj, GX_TEXMAP0);

	GX_SetTevColor(GX_TEVREG1,FONT_COLOR);

	GX_SetNumTevStages (1);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn (GX_TEVSTAGE0, GX_CC_C1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_A1, GX_CA_TEXA, GX_CA_ZERO);
	GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	//set blend mode
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetColorUpdate(GX_ENABLE);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

void draw_string(int x, int y, char *string, float scale, bool centered)
{
	if(centered)
	{
		int string_width = 0;
		int strHeight = (font_chars.fheight+STR_HEIGHT_OFFSET) * scale;
		char* string_work = string;

		while(*string_work)
		{
			unsigned char c = *string_work;
			string_width += (int) font_chars.font_size[c] * scale;
			string_work++;
		}
		x = (int) x - string_width/2;
		y = (int) y - strHeight/2;
	}

	while (*string)
	{
		unsigned char c = *string;
		int i;
		GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
		for (i=0; i<4; i++) {
			int s = (i & 1) ^ ((i & 2) >> 1) ? font_chars.font_size[c] : 1;
			int t = (i & 2) ? font_chars.fheight : 1;
			float s0 = ((float) (font_chars.s[c] + s))/512;
			float t0 = ((float) (font_chars.t[c] + t))/512;
			s = (int) s * scale;
			t = (int) t * scale;
			GX_Position3s16(x + s, y + t, 0);
			GX_Color4u8(FONT_COLOR.r, FONT_COLOR.g, FONT_COLOR.b, FONT_COLOR.a);
			GX_TexCoord2f32(s0, t0);
		}
		GX_End();

		x += (int) font_chars.font_size[c] * scale;
		string++;
	}

}

void font_write_styled(int x, int y, char *string, float size, bool centered, GXColor color)
{
	FONT_COLOR = color;
	draw_font_initialise(FONT_COLOR);
	draw_string(x, y, string, size, centered);
}

void font_write(int x, int y, char *string)
{
	font_write_styled(x, y, string, 1.0f, false, (GXColor) {255, 255, 255, 255});
	FONT_COLOR = (GXColor) {255, 255, 255, 255};
	draw_font_initialise(FONT_COLOR);
	draw_string(x, y, string, 1.0f, false);
}

int font_text_size_in_pixels(char *string)
{
	int string_width = 0;
	float scale = 1.0f;
	char* string_work = string;
	while(*string_work)
	{
		unsigned char c = *string_work;
		string_width += (int) font_chars.font_size[c] * scale;
		string_work++;
	}
	return string_width;
}

float font_text_scale_to_fit_width(char *string, int width) {
	int string_width = 0;
	char* string_work = string;
	while(*string_work)
	{
		unsigned char c = *string_work;
		string_width += (int) font_chars.font_size[c] * 1.0f;
		string_work++;
	}
	return width>string_width ? 1.0f : (float)((float)width/(float)string_width);
}

void font_write_center(int y, char *string) {
	int x = font_text_size_in_pixels(string);
	if (x > back_framewidth)
		x = back_framewidth;
	x = (back_framewidth - x) >> 1;
	font_write(x, y, string);
}
