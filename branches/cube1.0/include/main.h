/**
 * CleanRip - main.h
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

#ifndef MAIN_H
#define MAIN_H
#include <gccore.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <ogcsys.h>

#define NGC_MAGIC 0xC2339F3D

#define TYPE_SLOTA 0
#define TYPE_SLOTB 1

#define OPT_READ_SIZE    0x000080

#define NGC_DISC_SIZE   0x0AE0B0

#define MAX_NGC_OPTIONS 3

// Version info
#define V_MAJOR			1
#define V_MID			0
#define V_MINOR			4

/*** 2D Video Globals ***/
extern GXRModeObj *vmode;	/*** Graphics Mode Object ***/
extern u32 *xfb[2];			/*** Framebuffers ***/
extern int whichfb;			/*** Frame buffer toggle ***/
extern unsigned int iosversion;
extern int verify_in_use;
extern int verify_disc_type;

u32 get_buttons_pressed();

enum options
{
	NGC_SHRINK_ISO=0,
	NGC_ALIGN_FILES,
	NGC_ALIGN_BOUNDARY
};

enum shrinkOptions
{
  SHRINK_NONE=0,
  SHRINK_PAD_GARBAGE,
  SHRINK_ALL,
  SHRINK_DELIM
};

enum alignOptions
{
  ALIGN_ALL=0,
  ALIGN_AUDIO,
  ALIGN_DELIM
};

enum alignBoundaryOptions
{
  ALIGN_32=0,
  ALIGN_2,
  ALIGN_512,
  ALIGNB_DELIM
};

#endif

