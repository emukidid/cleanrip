/**
 * CleanRip - FrameBufferMagic.c
 * Copyright (C) 2010-2026 emu_kidid
 *
 * Framebuffer routines for drawing
 *
 * CleanRip homepage: https://github.com/emukidid/cleanrip
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
#include <math.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "main.h"
#include "verify.h"

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

extern u32 iosversion;

// internal helper funcs
char *typeToStr(int type) {
	switch (type) {
	case D_WARN:
		return "(Warning)";
	case D_INFO:
		return "(Info)";
	case D_FAIL:
		return "(Error!)";
	case D_PASS:
		return "(Success)";
	}
	return "";
}

#define GUI_MSGBOX_ALPHA 200

TPLFile backdropTPL;
GXTexObj backdropTexObj;
TPLFile btnaTPL;
GXTexObj btnaTexObj;
TPLFile btnbTPL;
GXTexObj btnbTexObj;
TPLFile boxinnerTPL;
GXTexObj boxinnerTexObj;
TPLFile boxouterTPL;
GXTexObj boxouterTexObj;

void init_textures() 
{
	TPL_OpenTPLFromMemory(&backdropTPL, (void *)backdrop_tpl, backdrop_tpl_size);
	TPL_GetTexture(&backdropTPL,backdrop,&backdropTexObj);
	TPL_OpenTPLFromMemory(&btnaTPL, (void *)btna_tpl, btna_tpl_size);
	TPL_GetTexture(&btnaTPL,btna,&btnaTexObj);
	TPL_OpenTPLFromMemory(&btnbTPL, (void *)btnb_tpl, btnb_tpl_size);
	TPL_GetTexture(&btnbTPL,btnb,&btnbTexObj);
	TPL_OpenTPLFromMemory(&boxinnerTPL, (void *)boxinner_tpl, boxinner_tpl_size);
	TPL_GetTexture(&boxinnerTPL,boxinner,&boxinnerTexObj);
	TPL_OpenTPLFromMemory(&boxouterTPL, (void *)boxouter_tpl, boxouter_tpl_size);
	TPL_GetTexture(&boxouterTPL,boxouter,&boxouterTexObj);
}

void drawInit()
{
	Mtx44 GXprojection2D;
	Mtx GXmodelView2D;

	// Reset various parameters from gfx plugin
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
//	GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	guMtxIdentity(GXmodelView2D);
	GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	guOrtho(GXprojection2D, 0, 479, 0, 639, 0, 700);
	GX_LoadProjectionMtx(GXprojection2D, GX_ORTHOGRAPHIC);
//	GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);

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
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); //Fix src alpha
	GX_SetColorUpdate(GX_ENABLE);
//	GX_SetAlphaUpdate(GX_ENABLE);
//	GX_SetDstAlpha(GX_DISABLE, 0xFF);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

void drawRect(int x, int y, int width, int height, int depth, GXColor color, float s0, float s1, float t0, float t1)
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

void DrawSimpleBox(int x, int y, int width, int height, int depth, GXColor fillColor, GXColor borderColor) 
{
	//Adjust for blank texture border
	x-=4; y-=4; width+=8; height+=8;
	
	drawInit();
	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
	GX_InvalidateTexAll();
	GX_LoadTexObj(&boxinnerTexObj, GX_TEXMAP0);

	drawRect(x, y, width/2, height/2, depth, fillColor, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	drawRect(x+(width/2), y, width/2, height/2, depth, fillColor, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	drawRect(x, y+(height/2), width/2, height/2, depth, fillColor, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	drawRect(x+(width/2), y+(height/2), width/2, height/2, depth, fillColor, ((float)width/32), 0.0f, ((float)height/32), 0.0f);

	GX_InvalidateTexAll();
	GX_LoadTexObj(&boxouterTexObj, GX_TEXMAP0);

	drawRect(x, y, width/2, height/2, depth, borderColor, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	drawRect(x+(width/2), y, width/2, height/2, depth, borderColor, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	drawRect(x, y+(height/2), width/2, height/2, depth, borderColor, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	drawRect(x+(width/2), y+(height/2), width/2, height/2, depth, borderColor, ((float)width/32), 0.0f, ((float)height/32), 0.0f);
}

void DrawImage(int textureId, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2)
{
	drawInit();
	GX_SetTevOp (GX_TEVSTAGE0, GX_REPLACE);
	GX_InvalidateTexAll();

	switch(textureId)
	{
	case TEX_BACKDROP:
		GX_LoadTexObj(&backdropTexObj, GX_TEXMAP0);
		break;
	case TEX_BTNA:
		GX_LoadTexObj(&btnaTexObj, GX_TEXMAP0);
		break;
	case TEX_BTNB:
		GX_LoadTexObj(&btnbTexObj, GX_TEXMAP0);
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

void DrawAButton(int x, int y) {
	DrawImage(TEX_BTNA, x, y, 36, 36, 0, 0.0f, 1.0f, 0.0f, 1.0f);
}

void DrawBButton(int x, int y) {
	DrawImage(TEX_BTNB, x, y, 36, 36, 0, 0.0f, 1.0f, 0.0f, 1.0f);
}

#define DISC_SEGMENTS 64
static void DrawRingGradient(
    float cx, float cy, float depth,
    float innerR, float outerR,
    GXColor innerColor,
    GXColor outerColor
) {
    float angleStep = (2.0f * M_PI) / DISC_SEGMENTS;

    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (DISC_SEGMENTS + 1) * 2);

    for (int i = 0; i <= DISC_SEGMENTS; i++) {
        float a  = i * angleStep;
        float ca = cosf(a);
        float sa = sinf(a);

        float ix = cx + ca * innerR;
        float iy = cy + sa * innerR;

        float ox = cx + ca * outerR;
        float oy = cy + sa * outerR;

        GX_Position3f32(ix, iy, depth);
        GX_Color4u8(innerColor.r, innerColor.g, innerColor.b, innerColor.a);
        GX_TexCoord2f32(0.0f, 0.0f);

        GX_Position3f32(ox, oy, depth);
        GX_Color4u8(outerColor.r, outerColor.g, outerColor.b, outerColor.a);
        GX_TexCoord2f32(0.0f, 0.0f);
    }

    GX_End();
}

static void DrawSpecularArc(
    float cx, float cy, float depth,
    float innerR, float outerR,
    float startAngle, float endAngle,
    GXColor color
) {
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (DISC_SEGMENTS + 1) * 2);

    for (int i = 0; i <= DISC_SEGMENTS; i++) {
        float t = (float)i / (float)DISC_SEGMENTS;
        float a = startAngle + t * (endAngle - startAngle);

        float ca = cosf(a);
        float sa = sinf(a);

        // Fade alpha toward edges of arc
        GXColor fade = color;
        fade.a = (u8)(color.a * (1.0f - fabsf(t - 0.5f) * 2.0f));

        float ix = cx + ca * innerR;
        float iy = cy + sa * innerR;

        float ox = cx + ca * outerR;
        float oy = cy + sa * outerR;

        GX_Position3f32(ix, iy, depth);
        GX_Color4u8(fade.r, fade.g, fade.b, fade.a);
        GX_TexCoord2f32(0.0f, 0.0f);

        GX_Position3f32(ox, oy, depth);
        GX_Color4u8(fade.r, fade.g, fade.b, fade.a);
        GX_TexCoord2f32(0.0f, 0.0f);
    }

    GX_End();
}

static void DrawRing(
    float cx, float cy, float depth,
    float innerR, float outerR,
    GXColor color
) {
    float angleStep = (2.0f * M_PI) / DISC_SEGMENTS;

    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (DISC_SEGMENTS + 1) * 2);

    for (int i = 0; i <= DISC_SEGMENTS; i++) {
        float a  = i * angleStep;
        float ca = cosf(a);
        float sa = sinf(a);

        float ix = cx + ca * innerR;
        float iy = cy + sa * innerR;

        float ox = cx + ca * outerR;
        float oy = cy + sa * outerR;

        GX_Position3f32(ix, iy, depth);
        GX_Color4u8(color.r, color.g, color.b, color.a);
        GX_TexCoord2f32(0.0f, 0.0f);

        GX_Position3f32(ox, oy, depth);
        GX_Color4u8(color.r, color.g, color.b, color.a);
        GX_TexCoord2f32(0.0f, 0.0f);
    }

    GX_End();
}


void DrawDiscPercent(
    float cx, float cy, float depth,
    float innerRadius, float outerRadius,
    int percentage,
    GXColor fillColor,
    float originX, float originY, bool isDualLayer
) {
    drawInit();
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    float diameter = outerRadius * 2.0f;
    float ox = cx - diameter * originX;
    float oy = cy - diameter * originY;

    float centerX = ox + outerRadius;
    float centerY = oy + outerRadius;

    float r_clear_inner  = innerRadius;
    float r_mirror_start = innerRadius + (outerRadius - innerRadius) * 0.10f;
    float r_data_start   = innerRadius + (outerRadius - innerRadius) * 0.25f;
    float r_outer_clear  = outerRadius - (outerRadius - innerRadius) * 0.05f;

    GXColor col_clear_inner = (GXColor){220,220,220,80};
    GXColor col_mirror = (GXColor){200,200,200,255};
    GXColor col_data = (GXColor){200, 185, 120, 255};
    GXColor col_clear_outer = (GXColor){220,220,220,60};

    // Mirror ring, data start, data and data end areas
    DrawRing(centerX, centerY, depth, r_clear_inner, r_mirror_start, col_clear_inner);
    DrawRing(centerX, centerY, depth, r_mirror_start, r_data_start, col_mirror);
    DrawRing(centerX, centerY, depth, r_data_start, r_outer_clear, col_data);
    DrawRing(centerX, centerY, depth, r_outer_clear, outerRadius, col_clear_outer);
	
	// Data area gradient
	GXColor dataInner = (GXColor){225, 210, 160, 255};
	GXColor dataOuter = (GXColor){180, 165, 120, 255};

	DrawRingGradient(centerX, centerY, depth,
					 r_data_start, r_outer_clear,
					 dataInner, dataOuter);

	// Arc to give it some shine
	GXColor highlight = (GXColor){255, 255, 255, 120};
	DrawSpecularArc(centerX, centerY, depth - 0.002f,
					r_data_start, r_outer_clear,
					20.0f * M_PI / 180.0f,
					70.0f * M_PI / 180.0f,
					highlight);

	// Progress overlay drawing
	
	// Special dual layer progress overlay
	if(isDualLayer) {
		if(percentage > 50) {
			// draw the first layer as done.
			float fillOuter =
				r_data_start + (r_outer_clear - r_data_start) * 1.0f;

			DrawRing(centerX, centerY, depth - 0.001f,
					 r_data_start, fillOuter,
					 fillColor);
			
			percentage = (percentage - 50) * 2;
			fillColor = (GXColor){fillColor.r, fillColor.g, fillColor.b, 225};
		}
		else {
			percentage = percentage * 2;
		}
	}
	// regular percentage overlay
	if (percentage > 0) {
		float p = (float)percentage / 100.0f;

		float fillOuter =
			r_data_start + (r_outer_clear - r_data_start) * p;

		DrawRing(centerX, centerY, depth - 0.001f,
				 r_data_start, fillOuter,
				 fillColor);
	}
}

void _DrawBackdrop() {
	char iosStr[256];
	DrawImage(TEX_BACKDROP, 0, 0, 640, 480, 0, 0.0f, 1.0f, 0.0f, 1.0f);
#ifdef HW_RVL
	sprintf(iosStr, "IOS %u", iosversion);
	WriteFont(520, 40, iosStr);
#endif
#ifdef HW_DOL
	WriteFont(510, 40, "GameCube");
#endif

	if(V_MINOR) {
		sprintf(iosStr, "v%i.%i.%i by emu_kidid", V_MAJOR,V_MID,V_MINOR);
	}
	else {
		sprintf(iosStr, "v%i.%i by emu_kidid", V_MAJOR,V_MID);
	}
	WriteFont(225, 40, iosStr);
}

// Externally accessible functions

// Call this when starting a screen
void DrawFrameStart() {
  whichfb ^= 1;
  _DrawBackdrop();
}

// Call this at the end of a screen
void DrawFrameFinish() {
	//Copy EFB->XFB
	GX_SetCopyClear((GXColor){0, 0, 0, 0xFF}, GX_MAX_Z24);
	GX_CopyDisp(xfb[whichfb],GX_TRUE);
	GX_Flush();

	VIDEO_SetNextFramebuffer(xfb[whichfb]);
	VIDEO_Flush();
 	VIDEO_WaitVSync();
	if(vmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
}

void DrawDatInfo(int disc_type) {
	if (verify_type_in_use == VERIFY_REDUMP_DAT_GC) {
			WriteCentre(440, "Gamecube Redump.org DAT in use");
	}
	else if(verify_type_in_use == VERIFY_REDUMP_DAT_WII) {
		WriteCentre(440, "Wii Redump.org DAT in use");
	}
	else {
		sprintf(txtbuffer, "Internal CRC list in use (%s)", verify_get_internal_updated(disc_type));
		WriteCentre(440, txtbuffer);
	}
}

void DrawProgressBar(int percent, char *message, int discType) {
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int middleY = (y2+y1)/2;
	float scale = GetTextScaleToFitInWidth(message, x2-x1);
  	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //black
  	GXColor noColor = (GXColor) {0,0,0,0}; //blank
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	GXColor progressBarColor = (GXColor) {255,128,0,GUI_MSGBOX_ALPHA}; //orange
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor); 
	int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
	int progressBarWidth = multiplier*100;
	DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
			(multiplier*100), 20, 0, noColor, borderColor); 
	DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
			(multiplier*percent), 20, 0, progressBarColor, noColor); 

	WriteFontStyled(640/2, middleY, message, scale, true, defaultColor);
	sprintf(txtbuffer,"%d %% complete",percent);
	WriteFontStyled(640/2, middleY+30, txtbuffer, 1.0f, true, defaultColor);
	
	DrawDatInfo(discType);
}

void DrawProgressDetailed(int percent, char *message, int startMb, int endMb, char *discTypeStr, int calculateCheckSums, int discType) {
	int x1 = ((640/2) - (PROGRESS_BOX_DETAILED_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_DETAILED_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_DETAILED_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_DETAILED_HEIGHT/2));
  	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor); 
	int numLines = 1;

	for (int i = 0; message[i]; i++)
		if (message[i] == '\n')
			numLines++;

	int startY = ((y2+y1)/2) - ((numLines*24)/2);
	char *tok = strtok(message,"\n");
	while(tok != NULL) {
		WriteFontStyled(x1 + 20, startY, tok, 0.85f, false, defaultColor);
		tok = strtok(NULL,"\n");
		startY+=24;
	}
	sprintf(txtbuffer,"%s disc", discTypeStr);
	WriteFontStyled(x1 + 20, y1 + 10, txtbuffer, 1.0f, false, defaultColor);

	sprintf(txtbuffer,"%04d", startMb);
	WriteFontStyled(x2 - 190, y1 + 10, txtbuffer, 0.75f, false, defaultColor);
	sprintf(txtbuffer,"/ %d MB", endMb);
	WriteFontStyled(x2 - 130, y1 + 10, txtbuffer, 0.75f, false, defaultColor);
	DrawDiscPercent(480, 240, 0,
			20, 90,
			percent,
			(GXColor){0,200,200,180},
			0.5f, 0.5f, endMb > 8000);
	sprintf(txtbuffer,"%d %%",percent);
	WriteFontStyled(x2 - 140, y2-24, txtbuffer, 0.75f, false, defaultColor);
	
	WriteFontStyled(x1 + 20, y2-72, calculateCheckSums ? "Calcs: CRC32/MD5/SHA-1" : "Calcs: CRC32", 0.75f, false, defaultColor);
	WriteFontStyled(x1 + 20, y2-24, "Press B to cancel", 0.75f, false, defaultColor);
	DrawDatInfo(discType);
}

void DrawMessageBox(int type, char *message) 
{
	int x1 = ((640/2) - (MESSAGE_BOX_WIDTH/2));
	int x2 = ((640/2) + (MESSAGE_BOX_WIDTH/2));
	int y1 = ((480/2) - (MESSAGE_BOX_HEIGHT/2));
	int y2 = ((480/2) + (MESSAGE_BOX_HEIGHT/2));
	int middleY = y2-y1 < 23 ? y1+3 : (y2+y1)/2-12;
	
  	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	DrawFrameStart();
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor); 

	char *tok = strtok(message,"\n");
	while(tok != NULL) {
		WriteFontStyled(640/2, middleY, tok, 1.0f, true, defaultColor);
		tok = strtok(NULL,"\n");
		middleY+=24;
	}
	DrawFrameFinish();
}

void DrawRawFont(int x, int y, char *message) {
  WriteFont(x, y, message);
}

void DrawSelectableButton(int x1, int y1, int x2, int y2, char *message, int mode, u32 color) 
{
	int middleY, borderSize;
	color = (color == -1) ? BUTTON_COLOUR_INNER : color; //never used

	borderSize = (mode==B_SELECTED) ? 6 : 4;
	middleY = (((y2-y1)/2)-12)+y1;

	//determine length of the text ourselves if x2 == -1
	x1 = (x2 == -1) ? x1+2:x1;
	x2 = (x2 == -1) ? GetTextSizeInPixels(message)+x1+(borderSize*2)+6 : x2;

	if(middleY+24 > y2) {
		middleY = y1+3;
	}

	GXColor selectColor = (GXColor) {96,107,164,GUI_MSGBOX_ALPHA}; //bluish
	GXColor noColor = (GXColor) {0,0,0,0}; //black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	//Draw Text and backfill (if selected)
	if(mode==B_SELECTED) {
		DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, selectColor, borderColor);
		WriteFontStyled(x1 + borderSize+3, middleY, message, 1.0f, false, defaultColor);
	}
	else {
		DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, noColor, borderColor);
		WriteFontStyled(x1 + borderSize+3, middleY, message, 1.0f, false, defaultColor);
	}
}

void DrawEmptyBox(int x1, int y1, int x2, int y2, int color) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;

	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //Black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //Silver
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor);
}

int DrawYesNoDialog(char *line1, char *line2) {
	int selection = 0;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		int xlen = (vmode->fbWidth - 38) - 30;
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(230, line1);
		WriteCentre(255, line2);
		DrawSelectableButton((xlen/3), 310, -1, 340, "Yes", (selection) ? B_SELECTED : B_NOSELECT, -1);
		DrawSelectableButton((vmode->fbWidth - 38) - (xlen/3), 310, -1, 340, "No", (!selection) ? B_SELECTED : B_NOSELECT, -1);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			selection ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			selection ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	return selection;
}

