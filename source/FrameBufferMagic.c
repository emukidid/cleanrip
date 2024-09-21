/**
 * CleanRip - FrameBufferMagic.c
 * Copyright (C) 2010 emu_kidid
 *
 * Framebuffer routines for drawing
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/exi.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "main.h"

#include "backdrop_tpl.h"
#include "backdrop.h"
#include "btna_tpl.h"
#include "btna.h"
#include "btnb_tpl.h"
#include "btnb.h"
#include "boxinner_tpl.h"
#include "boxinner.h"
#include "boxouter_tpl.h"
#include "boxouter.h"

extern u32 ios_version;

#define GUI_MSGBOX_ALPHA 200

TPLFile backdrop_TPL;
GXTexObj backdrop_tex_obj;
TPLFile btn_A_TPL;
GXTexObj btn_A_tex_obj;
TPLFile btn_B_TPL;
GXTexObj btn_B_tex_obj;
TPLFile box_inner_TPL;
GXTexObj box_inner_tex_obj;
TPLFile box_outer_TPL;
GXTexObj box_outer_tex_obj;

GXColor SELECT_COLOR = (GXColor) {96,107,164,GUI_MSGBOX_ALPHA}; //bluish
GXColor BORDER_COLOR = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
GXColor PROGRESS_BAR_COLOR = (GXColor) {255,128,0,GUI_MSGBOX_ALPHA}; //orange
GXColor BLACK_COLOR = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //black
GXColor NO_COLOR = (GXColor) {0,0,0,0}; //blank


void fbm_initialise(void) 
{
	TPL_OpenTPLFromMemory(&backdrop_TPL, (void *)backdrop_tpl, backdrop_tpl_size);
	TPL_GetTexture(&backdrop_TPL,backdrop,&backdrop_tex_obj);
	TPL_OpenTPLFromMemory(&btn_A_TPL, (void *)btna_tpl, btna_tpl_size);
	TPL_GetTexture(&btn_A_TPL,btna,&btn_A_tex_obj);
	TPL_OpenTPLFromMemory(&btn_B_TPL, (void *)btnb_tpl, btnb_tpl_size);
	TPL_GetTexture(&btn_B_TPL,btnb,&btn_B_tex_obj);
	TPL_OpenTPLFromMemory(&box_inner_TPL, (void *)boxinner_tpl, boxinner_tpl_size);
	TPL_GetTexture(&box_inner_TPL,boxinner,&box_inner_tex_obj);
	GX_InitTexObjWrapMode(&box_inner_tex_obj, GX_CLAMP, GX_CLAMP);
	TPL_OpenTPLFromMemory(&box_outer_TPL, (void *)boxouter_tpl, boxouter_tpl_size);
	TPL_GetTexture(&box_outer_TPL,boxouter,&box_outer_tex_obj);
	GX_InitTexObjWrapMode(&box_outer_tex_obj, GX_CLAMP, GX_CLAMP);
}

void draw_initialise()
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
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	//enable textures
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_SetNumTevStages (1);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);

	//set blend mode
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetColorUpdate(GX_ENABLE);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

void draw_rect(int x, int y, int width, int height, int depth, GXColor color, float s0, float s1, float t0, float t1)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t0);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t0);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t1);
	GX_End();
}

void draw_simple_box(int x, int y, int width, int height, int depth, GXColor fill_color, GXColor border_color) 
{
	//Adjust for blank texture border
	x-=4; y-=4; width+=8; height+=8;
	
	draw_initialise();
	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
	GX_InvalidateTexAll();
	GX_LoadTexObj(&box_inner_tex_obj, GX_TEXMAP0);

	draw_rect(x, y, width/2, height/2, depth, fill_color, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	draw_rect(x+(width/2), y, width/2, height/2, depth, fill_color, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	draw_rect(x, y+(height/2), width/2, height/2, depth, fill_color, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	draw_rect(x+(width/2), y+(height/2), width/2, height/2, depth, fill_color, ((float)width/32), 0.0f, ((float)height/32), 0.0f);

	GX_InvalidateTexAll();
	GX_LoadTexObj(&box_outer_tex_obj, GX_TEXMAP0);

	draw_rect(x, y, width/2, height/2, depth, border_color, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	draw_rect(x+(width/2), y, width/2, height/2, depth, border_color, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	draw_rect(x, y+(height/2), width/2, height/2, depth, border_color, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	draw_rect(x+(width/2), y+(height/2), width/2, height/2, depth, border_color, ((float)width/32), 0.0f, ((float)height/32), 0.0f);
}

void draw_image(int texture_id, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2)
{
	draw_initialise();
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_InvalidateTexAll();

	switch(texture_id)
	{
	case TEX_BACKDROP:
		GX_LoadTexObj(&backdrop_tex_obj, GX_TEXMAP0);
		break;
	case TEX_BTNA:
		GX_LoadTexObj(&btn_A_tex_obj, GX_TEXMAP0);
		break;
	case TEX_BTNB:
		GX_LoadTexObj(&btn_B_tex_obj, GX_TEXMAP0);
		break;
	}	

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t1);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t2);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t2);
	GX_End();
}

void fbm_draw_A_button(int x, int y) {
	draw_image(TEX_BTNA, x, y, 36, 36, 0, 0.0f, 1.0f, 0.0f, 1.0f);
}

void fbm_draw_B_button(int x, int y) {
	draw_image(TEX_BTNB, x, y, 36, 36, 0, 0.0f, 1.0f, 0.0f, 1.0f);
}

void draw_backdrop(void) {
	char iosStr[256];
	draw_image(TEX_BACKDROP, 0, 0, 640, 480, 0, 0.0f, 1.0f, 0.0f, 1.0f);
#ifdef HW_RVL
	sprintf(iosStr, "IOS %u", ios_version);
	font_write(520, 40, iosStr);
#endif
#ifdef HW_DOL
	font_write(510, 40, "GameCube");
#endif

	sprintf(iosStr, "v%i.%i.%i by emu_kidid", V_MAJOR,V_MID,V_MINOR);
	font_write(225, 40, iosStr);
	if (verify_in_use) {
		if(verify_disc_type == IS_NGC_DISC) {
			font_write_center(440, "Gamecube Redump.org DAT in use");
		}
		else if(verify_disc_type == IS_WII_DISC) {
			font_write_center(440, "Wii Redump.org DAT in use");
		}
	}
}

// Call this when starting a screen
void fbm_frame_start(void) {
  which_fb ^= 1;
  draw_backdrop();
}

// Call this at the end of a screen
void fbm_frame_finish(void) {
	//Copy EFB->XFB
	GX_SetCopyClear((GXColor){0, 0, 0, 0xFF}, GX_MAX_Z24);
	GX_CopyDisp(xfb[which_fb],GX_TRUE);
	GX_Flush();

	VIDEO_SetNextFramebuffer(xfb[which_fb]);
	VIDEO_Flush();
 	VIDEO_WaitVSync();
	if(vmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
}

void fbm_draw_progress_bar(int percent, char *message) {
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int y_center = (y2+y1)/2;
	float scale = font_text_scale_to_fit_width(message, x2-x1);
  	
	
	draw_simple_box( x1, y1, x2-x1, y2-y1, 0, BLACK_COLOR, BORDER_COLOR); 
	int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
	int progressBarWidth = multiplier*100;
	draw_simple_box( (640/2 - progressBarWidth/2), y1+20,
			(multiplier*100), 20, 0, NO_COLOR, BORDER_COLOR); 
	draw_simple_box( (640/2 - progressBarWidth/2), y1+20,
			(multiplier*percent), 20, 0, PROGRESS_BAR_COLOR, NO_COLOR); 

	font_write_styled(640/2, y_center, message, scale, true, DEFAULT_COLOR);
	sprintf(txtbuffer,"%d %% complete",percent);
	font_write_styled(640/2, y_center+30, txtbuffer, 1.0f, true, DEFAULT_COLOR);
}

void fbm_draw_msg_box(int type, char *message) 
{
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int y_center = y2-y1 < 23 ? y1+3 : (y2+y1)/2-12;
	
	fbm_frame_start();
	draw_simple_box( x1, y1, x2-x1, y2-y1, 0, BLACK_COLOR, BORDER_COLOR); 

	char *tok = strtok(message,"\n");
	while(tok != NULL) {
		font_write_styled(640/2, y_center, tok, 1.0f, true, DEFAULT_COLOR);
		tok = strtok(NULL,"\n");
		y_center+=24;
	}
	fbm_frame_finish();
}

void fbm_draw_raw_font(int x, int y, char *message) {
  font_write(x, y, message);
}

void fbm_draw_selection_button(int x1, int y1, int x2, int y2, char *message, int mode, u32 color) 
{
	int y_center, border_size;
	color = (color == -1) ? BUTTON_COLOUR_INNER : color; //never used

	border_size = (mode==B_SELECTED) ? 6 : 4;
	y_center = (((y2-y1)/2)-12)+y1;

	//determine length of the text ourselves if x2 == -1
	x1 = (x2 == -1) ? x1+2:x1;
	x2 = (x2 == -1) ? font_text_size_in_pixels(message)+x1+(border_size*2)+6 : x2;

	if(y_center+24 > y2) {
		y_center = y1+3;
	}
	
	//Draw Text and backfill (if selected)
	if(mode==B_SELECTED) {
		draw_simple_box( x1, y1, x2-x1, y2-y1, 0, SELECT_COLOR, BORDER_COLOR);
		font_write_styled(x1 + border_size+3, y_center, message, 1.0f, false, DEFAULT_COLOR);
	}
	else {
		draw_simple_box( x1, y1, x2-x1, y2-y1, 0, NO_COLOR, BORDER_COLOR);
		font_write_styled(x1 + border_size+3, y_center, message, 1.0f, false, DEFAULT_COLOR);
	}
}

void fbm_draw_box(int x1, int y1, int x2, int y2)
{
	int border_size;
	border_size = (y2-y1) <= 30 ? 3 : 10;
	x1-=border_size;x2+=border_size;y1-=border_size;y2+=border_size;
	
	draw_simple_box( x1, y1, x2-x1, y2-y1, 0, BLACK_COLOR, BORDER_COLOR);
}

int fbm_draw_yes_no_dialog(char *line1, char *line2) {
	int selection = 0;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		fbm_frame_start();
		int x_len = (vmode->fbWidth - 38) - 30;
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(230, line1);
		font_write_center(255, line2);
		fbm_draw_selection_button((x_len/3), 310, -1, 340, "Yes", (selection) ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button((vmode->fbWidth - 38) - (x_len/3), 310, -1, 340, "No", (!selection) ? B_SELECTED : B_NOSELECT, -1);
		fbm_frame_finish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 buttons = get_buttons_pressed();
		if (buttons & PAD_BUTTON_RIGHT)
			selection ^= 1;
		if (buttons & PAD_BUTTON_LEFT)
			selection ^= 1;
		if (buttons & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	return selection;
}

