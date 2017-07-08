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

#define WII_MAGIC 0x5D1C9EA3
#define NGC_MAGIC 0xC2339F3D

#define TYPE_USB 0
#define TYPE_SD 1

#define TYPE_FAT 0
#define TYPE_NTFS 1

#define READ_SIZE		0x10000
#define ONE_MEGABYTE    0x000200
#define ONE_GIGABYTE    0x080000

#define NGC_DISC_SIZE   0x0AE0B0
#define WII_D5_SIZE     0x230480
#define WII_D9_SIZE     0x3F69C0

#define HW_REG_BASE   	0xcd800000
#define HW_ARMIRQMASK 	(HW_REG_BASE + 0x03c)
#define HW_ARMIRQFLAG 	(HW_REG_BASE + 0x038)

#define MAX_WII_OPTIONS 3
#define MAX_NGC_OPTIONS 3

// Version info
#define V_MAJOR			2
#define V_MID			0
#define V_MINOR			0

/*** 2D Video Globals ***/
extern GXRModeObj *vmode;	/*** Graphics Mode Object ***/
extern u32 *xfb[2];			/*** Framebuffers ***/
extern int whichfb;			/*** Frame buffer toggle ***/
extern u32 iosversion;
extern int verify_in_use;
extern int verify_disc_type;

u32 get_buttons_pressed();
void print_gecko(const char* fmt, ...);

enum discTypes
{
	IS_NGC_DISC=0,
	IS_WII_DISC,
	IS_UNK_DISC
};

enum options
{
	NGC_SHRINK_ISO=0,
	NGC_ALIGN_FILES,
	NGC_ALIGN_BOUNDARY,
	WII_DUAL_LAYER,
	WII_CHUNK_SIZE,
	WII_NEWFILE
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

enum dualOptions
{
  SINGLE_LAYER=0,
  DUAL_LAYER,
  DUAL_DELIM
};

enum chunkOptions
{
  CHUNK_1GB=0,
  CHUNK_2GB,
  CHUNK_3GB,
  CHUNK_MAX,
  CHUNK_DELIM
};

enum newFileOptions
{
  ASK_USER=0,
  AUTO_CHUNK,
  NEWFILE_DELIM
};

#endif

