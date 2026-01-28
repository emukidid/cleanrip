/**
 * CleanRip - gc_dvd.c (originally from Cube64/Wii64)
 * Copyright (C) 2007 - 2026 emu_kidid
 *
 * DVD Reading support for GC/Wii
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
#include <stdlib.h>
#include <stdint.h>
#include <ogc/dvdlow.h>
#include <malloc.h>
#include <string.h>
#include <gccore.h>
#include <unistd.h>
#include <di/di.h>
#include <ogc/machine/processor.h>
#include "gc_dvd.h"
#include "main.h"
#include "datel.h"

/* DVD Stuff */
u32 dvd_hard_init = 0;
static u32 read_cmd = NORMAL;

#ifdef HW_RVL
volatile u32* dvd = (volatile u32*) 0xCD806000;
#else
char cached_bca[64] __attribute__((aligned(32)));
volatile u32* dvd = (volatile u32*)0xCC006000;
#endif

#ifdef HW_DOL
void dvd_unlock()
{
	dvd[0] |= 0x00000014;
	dvd[1] = 0;
	dvd[2] = 0xFF014D41;
	dvd[3] = 0x54534849;	//MATS
	dvd[4] = 0x54410200;	//HITA
	dvd[7] = 1;
	while (dvd[7] & 1);
	
	dvd[0] |= 0x00000014;
	dvd[1] = 0;
	dvd[2] = 0xFF004456;
	dvd[3] = 0x442D4741;	//DVD-
	dvd[4] = 0x4D450300;	//GAME
	dvd[7] = 1;
	while (dvd[7] & 1);
}

u32 dvd_readmem_32(u32 addr)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xFE010000;	
	dvd[3] = addr;
	dvd[4] = 0x00010000;	
	dvd[8] = 0;
	dvd[7] = 1;

	while (dvd[7] & 1);
	return dvd[8];
}

int dvd_readmem_array(u32 addr, void* buf, u32 size)
{
	u32* ptr = (u32*)buf;
	int rem = size;

	while (rem>0)
	{
		*ptr++ = dvd_readmem_32(addr);
		addr += 4;
		rem -= 4;
	}
	return 0;
}
#endif

int init_dvd() {
	// Gamecube Mode
#ifdef HW_DOL
	DVD_LowReset(DVD_RESETHARD);
	usleep(1150000);
	DVD_LowReset(DVD_RESETHARD);
	dvd_unlock();
	memset(cached_bca, 0, 64);
	dvd_readmem_array(0x415460, cached_bca, 64);
	dvd_read_id();
	if (!dvd_get_error()) {
		xeno_disable();
		return 0; //we're ok
	}
	if (dvd_get_error() >> 24) {
		return NO_DISC;
	}
	return -1;

#endif
	// Wii (Wii mode)
#ifdef HW_RVL
	STACK_ALIGN(u8,id,32,32);
	u32 error;

	// enable GPIO for spin-up on drive reset (active low)
	mask32(0x0D8000E0, 0x10, 0);
	// assert DI reset (active low)
	mask32(0x0D800194, 0x400, 0);
	usleep(1000);
	// deassert DI reset
	mask32(0x0D800194, 0, 0x400);

	error = dvd_get_error();
	if ((error >> 24) == 1) {
		return NO_DISC;
	}

	if ((!dvd_hard_init) || (dvd_get_error())) {
		// read id
		dvd[0] = 0x54;
		dvd[2] = 0xA8000040;
		dvd[3] = 0;
		dvd[4] = 0x20;
		dvd[5] = (u32)id & 0x1FFFFFFF;
		dvd[6] = 0x20;
		dvd[7] = 3;
		while (dvd[7] & 1)
			usleep(20000);
		dvd_hard_init = 1;
	}

	if ((dvd_get_error() & 0xFFFFFF) == 0x053000) {
		read_cmd = DVDR;
	} else {
		read_cmd = NORMAL;
	}
	dvd_read_id();

	return 0;
#endif
}

int dvd_read_id() {
#ifdef HW_RVL
	char readbuf[2048] __attribute__((aligned(32)));
	DVD_LowRead64(readbuf, 2048, 0ULL);
	memcpy((void*)0x80000000, readbuf, 32);
	return 0;
#endif
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xA8000040;
	dvd[3] = 0;
	dvd[4] = 0x20;
	dvd[5] = 0;
	dvd[6] = 0x20;
	dvd[7] = 3; // enable reading!
	while (dvd[7] & 1)
		LWP_YieldThread();
	if (dvd[0] & 0x4)
		return 1;
	return 0;
}

void dvd_read_bca(void* dst)
{
#ifdef HW_RVL
	dvd[2] = 0xDA000000;
	dvd[5] = (unsigned long)dst & 0x1FFFFFFF;
	dvd[6] = 0x40;
	dvd[7] = 3;
	DCInvalidateRange(dst, 64);
	while (dvd[7] & 1);
#else
	memcpy(dst, cached_bca, 64);
#endif
}


u32 dvd_get_error(void) {
	dvd[2] = 0xE0000000;
	dvd[8] = 0;
	dvd[7] = 1; // IMM
	while (dvd[7] & 1);
	return dvd[8];
}

void dvd_motor_off(int eject) {
#ifndef HW_RVL
	// Lets not tempt fate on GameCube
	eject = 0;
#endif
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xe3000000 | ((eject & 1) << 17);
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // IMM
	while (dvd[7] & 1);
}

/*
 DVD_LowRead64(void* dst, unsigned int len, uint64_t offset)
 Read Raw, needs to be on sector boundaries
 Has 8,796,093,020,160 byte limit (8 TeraBytes)
 Synchronous function.
 return -1 if offset is out of range
 */
int DVD_LowRead64(void* dst, u32 len, uint64_t offset) {
	if (offset >> 2 > 0xFFFFFFFF)
		return -1;

	if ((((u32) dst) & 0xC0000000) == 0x80000000) // cached?
		dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = read_cmd;
	dvd[3] = read_cmd == DVDR ? offset >> 11 : offset >> 2;
	dvd[4] = read_cmd == DVDR ? len >> 11 : len;
	dvd[5] = (u32) dst & 0x1FFFFFFF;
	dvd[6] = len;
	dvd[7] = 3; // enable reading!
	DCInvalidateRange(dst, len);
	while (dvd[7] & 1)
		LWP_YieldThread();

	if (dvd[0] & 0x4)
		return 1;
	return 0;
}

int DVD_LowRead64Datel(void* dst, u32 len, uint64_t offset, int isKnownDatel) {
	if (offset >> 2 > 0xFFFFFFFF)
		return -1;

	uint64_t discoffset = offset;
	u32 disclen = len;
	u32 fill = 0;
	datel_adjustStartStop(&discoffset, &disclen, &fill);
	if ((discoffset != offset) || (disclen != len))
		memset(dst, fill, len);
	if (disclen == 0) {
		return 0;
	}
		
	int try = 0;
	for (try = 0; try < 2; try++) {
		// This or Stop motor when skipping to prevent error.
		if (try > 0) 
			init_dvd();
		
		if ((((u32) dst) & 0xC0000000) == 0x80000000) // cached?
			dvd[0] = 0x2E;
		dvd[1] = 0;
		dvd[2] = read_cmd;
		dvd[3] = read_cmd == DVDR ? discoffset >> 11 : discoffset >> 2;
		dvd[4] = read_cmd == DVDR ? disclen >> 11 : disclen;
		dvd[5] = (u32) ((((u32)dst) + discoffset - offset) & 0x1FFFFFFF);
		dvd[6] = disclen;
		dvd[7] = 3; // enable reading!
		DCInvalidateRange(dst + discoffset - offset, disclen);
		while (dvd[7] & 1)
			LWP_YieldThread();
		if ((dvd[0] & 0x4) == 0)
			return 0;
	}

	if (dvd[0] & 0x4) {
		if (isKnownDatel)
			return 1;

		// Logic assumes READ_SIZE 0x10000
		datel_addSkip(offset & 0xFFFF0000, 0x00100000 - (offset & 0x000F0000)); // Test every start 0xXXX00000
		memset(dst, fill, len);
	}
	return 0;
}

// Disable XenoGC patching by reading a few sections that it will attempt to patch once
void xeno_disable() {
  char *readBuf = (char*)memalign(32,64*1024);
  if(!readBuf) {
    return;
  }
  DVD_LowRead64(readBuf, 64*1024, 0);           //xeno GC enable patching
  DVD_LowRead64(readBuf, 64*1024, 0x8000);   	//xeno GC disable patching
  DVD_LowRead64(readBuf, 64*1024, 0x1000000);   //xeno GC disable patching
  free(readBuf);
}

static char error_str[256];
char *dvd_error_str() {
	u32 err = dvd_get_error();
	if (!err)
		return "OK";

	memset(&error_str[0], 0, 256);
	switch (err >> 24) {
	case 0:
		break;
	case 1:
		strcpy(&error_str[0], "Lid open");
		break;
	case 2:
		strcpy(&error_str[0], "No disk/Disk changed");
		break;
	case 3:
		strcpy(&error_str[0], "No disk");
		break;
	case 4:
		strcpy(&error_str[0], "Motor off");
		break;
	case 5:
		strcpy(&error_str[0], "Disk not initialized");
		break;
	}
	switch (err & 0xFFFFFF) {
	case 0:
		break;
	case 0x020400:
		strcat(&error_str[0], " Motor Stopped");
		break;
	case 0x020401:
		strcat(&error_str[0], " Disk ID not read");
		break;
	case 0x023A00:
		strcat(&error_str[0], " Medium not present / Cover opened");
		break;
	case 0x030200:
		strcat(&error_str[0], " No Seek complete");
		break;
	case 0x031100:
		strcat(&error_str[0], " Unrecovered read error");
		break;
	case 0x040800:
		strcat(&error_str[0], " Transfer protocol error");
		break;
	case 0x052000:
		strcat(&error_str[0], " Invalid command operation code");
		break;
	case 0x052001:
		strcat(&error_str[0], " Audio Buffer not set");
		break;
	case 0x052100:
		strcat(&error_str[0], " Logical block address out of range");
		break;
	case 0x052400:
		strcat(&error_str[0], " Invalid Field in command packet");
		break;
	case 0x052401:
		strcat(&error_str[0], " Invalid audio command");
		break;
	case 0x052402:
		strcat(&error_str[0], " Configuration out of permitted period");
		break;
	case 0x053000:
		strcat(&error_str[0], " DVD-R"); //?
		break;
	case 0x053100:
		strcat(&error_str[0], " Wrong Read Type"); //?
		break;
	case 0x056300:
		strcat(&error_str[0], " End of user area encountered on this track");
		break;
	case 0x062800:
		strcat(&error_str[0], " Medium may have changed");
		break;
	case 0x0B5A01:
		strcat(&error_str[0], " Operator medium removal request");
		break;
	}
	if (!error_str[0])
		sprintf(&error_str[0], "Unknown error %08X", err);
	return &error_str[0];

}

