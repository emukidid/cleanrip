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
#include "drivecodes.h"
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

/*
 * NR disc support:
 *
 * The drive patch supports a new "NR mode". It programs the descrambler and servo the
 * way the NR reader does, forces "accept copy" (no lead-in/BCA checks), 
 * bypasses the disc auth NR discs fail and keeps the copy prot flags clear.
 *
 * NR discs are scrambled from frame byte 12, with the data starting at
 * byte 6, using a single ECMA-267 key per 16 sector block: preset (block
 * group XOR 9). The NR drive does this for every disc.
 * The retail drive descrambler can't produce that key: without the seed bit it
 * applies the standard key, with it the XOR of two consecutive keys. So the
 * drive is left on the standard key and told to deliver sectors that fail
 * the EDC check, and the GC swaps the keys from a table.
 * See drivecode/drivepatch.S for it all.
 */
#define PATCH_MODE_NR	2

static const u16 ecma_presets[16] = {
	0x0001, 0x5500, 0x0002, 0x2A00, 0x0004, 0x5400, 0x0008, 0x2800,
	0x0010, 0x5000, 0x0020, 0x2001, 0x0040, 0x4002, 0x0080, 0x0005
};

// NR key of block group g is preset (g XOR NR_KEY0)
#define NR_KEY0	9

static int nr_mode = 0;
static int nr_corr_ready = 0;	// nr_corr set up for this disc
static int nr_servo = -1;		// servo setup that initialised the disc, -1 if none yet
// Per block group: XOR that turns standard-key data into NR-key data,
// aligned to the 2048 bytes the host reads (frame byte 6 onwards)
static u32 nr_corr[16][2048 / 4];

// Standard debug drive stuff below
static void dvd_writemem_32(u32 addr, u32 dat)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xFE010100;
	dvd[3] = addr;
	dvd[4] = 0x00040000;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 3;
	while (dvd[7] & 1);

	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = dat;
	dvd[7] = 1;
	while (dvd[7] & 1);
}

static void dvd_writemem_array(u32 addr, const void* buf, u32 size)
{
	const u32* ptr = (const u32*)buf;
	int rem = size;

	while (rem > 0)
	{
		dvd_writemem_32(addr, *ptr++);
		addr += 4;
		rem -= 4;
	}
}

u32 dvd_drive_date()
{
	static u32 inquiry[8] __attribute__((aligned(32)));
	DCInvalidateRange(inquiry, 32);
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0x12000000;
	dvd[3] = 0;
	dvd[4] = 0x20;
	dvd[5] = (u32)inquiry & 0x1FFFFFFF;
	dvd[6] = 0x20;
	dvd[7] = 3;
	while (dvd[7] & 1);
	DCInvalidateRange(inquiry, 32);
	// Development drives report a non-zero revision and run other firmware
	if (inquiry[0] != 0)
		return 0;
	return inquiry[1];
}

static const char* drive_patch_ptr(u32 date)
{
	switch (date) {
	case 0x20020402: return Drive04;
	case 0x20010608: return Drive06;
	case 0x20020823: return Drive08;
	case 0x20010831: return DriveQ;
	}
	return NULL;
}

static void dvd_set_patch_mode(u32 mode)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0x55000000 | (mode << 16);
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1;
	while (dvd[7] & 1);
}

/* Disc ID read that gives up after timeout seconds: -2 timed out, 1 error */
static int nr_read_id_timeout(int timeout)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xA8000040;
	dvd[3] = 0;
	dvd[4] = 0x20;
	dvd[5] = 0;
	dvd[6] = 0x20;
	dvd[7] = 3;
	for (int i = 0; dvd[7] & 1; i++) {
		if (i >= timeout * 100) {
			return -2;
		}
		usleep(10000);
	}
	return (dvd[0] & 0x4) ? 1 : 0;
}

/* Read the disc ID, retrying for a while: 0 ok, -1 error, -2 drive hung */
static int nr_read_id(int tries)
{
	for (int i = 0; i < tries; i++) {
		if (nr_read_id_timeout(20) == -2) {
			print_gecko("NR read id timed out\r\n");
			DVD_LowReset(DVD_RESETHARD);
			return -2;
		}
		u32 err = dvd_get_error();
		if (!err) {
			return 0;
		}
		if ((err >> 24) == 1) {
			break;
		}
		usleep(500000);
	}
	return -1;
}

/* Firmware read options (FE 10): bits in mask are set to value */
static void dvd_set_read_options(u16 mask, u16 value)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xFE100000;
	dvd[3] = (mask << 16) | value;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1;
	while (dvd[7] & 1);
}
// Deliver sectors that fail the EDC check (error 0x1004) instead of failing
#define READ_OPT_IGNORE_EDC	0x0004

/* ECMA-267 scrambler key stream from an LFSR state */
static void ecma_key(u16 state, u8* out, int len)
{
	for (int i = 0; i < len; i++) {
		out[i] = state & 0xFF;
		for (int b = 0; b < 8; b++) {
			state = ((state << 1) | (((state >> 14) ^ (state >> 10)) & 1)) & 0x7FFF;
		}
	}
}

/* Build the key swap table for NR key = preset (group XOR key0) */
static void nr_set_keys(int key0)
{
	for (int g = 0; g < 16; g++) {
		// Frame bytes 12+ map to data bytes 6+ of what the host reads
		u8* corr = (u8*)nr_corr[g];
		memset(corr, 0, 6);
		ecma_key(ecma_presets[g] ^ ecma_presets[g ^ key0], corr + 6, 2048 - 6);
	}
}

/*
 * Check the fixed key on sector 0: the disc header magic at 0x1C must come
 * out right.
 */
static void nr_check_keys()
{
	static u8 buf[2048] __attribute__((aligned(32)));
	u8 key[0x20 - 6];

	if (DVD_LowRead64(buf, 2048, 0)) {
		print_gecko("NR key check: sector 0 read failed, using key %i\r\n", NR_KEY0);
		return;
	}
	for (int i = 0; i < 16; i++) {
		int k = i ^ NR_KEY0;	// NR_KEY0 first
		ecma_key(ecma_presets[0] ^ ecma_presets[k], key, sizeof(key));
		u32 magic = ((buf[0x1C] ^ key[0x1C - 6]) << 24) | ((buf[0x1D] ^ key[0x1D - 6]) << 16)
			| ((buf[0x1E] ^ key[0x1E - 6]) << 8) | (buf[0x1F] ^ key[0x1F - 6]);
		if (magic == 0xC2339F3D) {
			if (k != NR_KEY0) {
				print_gecko("NR key check: disc uses key %i, not %i\r\n", k, NR_KEY0);
				nr_set_keys(k);
			}
			return;
		}
	}
	print_gecko("NR key check: no key gives the disc magic, using key %i\r\n", NR_KEY0);
}

/* Swap the standard key for the NR key in sectors read from an NR disc */
static void nr_descramble(void* dst, u32 len, u64 offset)
{
	u32* p = (u32*)dst;
	u32 lba = offset >> 11;
	for (u32 s = 0; s < len / 2048; s++, lba++) {
		const u32* c = nr_corr[(lba >> 4) & 15];
		for (int i = 0; i < 2048 / 4; i++) {
			*p++ ^= c[i];
		}
	}
}

/* Upload the patch in NR mode, read the ID and set up host descrambling */
static int dvd_enable_nr_patches(int servo)
{
	static char patch[DRIVE_PATCH_SIZE] __attribute__((aligned(32)));
	const char* src = drive_patch_ptr(dvd_drive_date());
	if (!src) {
		return NR_UNSUPPORTED_DRIVE;
	}
	// Preset the mode so the patch arms NR mode before the firmware re-inits
	memcpy(patch, src, DRIVE_PATCH_SIZE);
	patch[DRIVE_PATCH_MODE_OFFSET] = PATCH_MODE_NR;
	patch[DRIVE_PATCH_SERVO_OFFSET] = servo;
	print_gecko("NR servo parameters: %s\r\n", servo ? "NR reader" : "retail");
	dvd_unlock();
	dvd_writemem_array(0xFF000000 | DRIVE_PATCH_ADDR, patch, DRIVE_PATCH_SIZE);
	// Point the IRQ vector  at the patch at 0x40d000,
	// it re-inits the drive with itself installed
	dvd_writemem_32(0x804c, 0x00d04000);
	dvd_set_patch_mode(PATCH_MODE_NR);
	dvd_unlock();
	dvd_set_read_options(READ_OPT_IGNORE_EDC, READ_OPT_IGNORE_EDC);

	// The firmware's own start after the re-init queues spin-up and the disc
	// ID step together, the patch supplies accept copy and authentication
	int ret = nr_read_id(20);
	if (ret == 0 && !nr_corr_ready) {
		nr_set_keys(NR_KEY0);
		nr_check_keys();
		nr_corr_ready = 1;
	}
	return ret ? -1 : 0;
}

void dvd_set_nr_mode(int enable) {
	nr_mode = enable;
	nr_corr_ready = 0;
	nr_servo = -1;
}

int dvd_get_nr_mode() {
	return nr_mode;
}

/*
 * Drive state for diagnosing a failed NR init: start-up progress code
 * (byte 2 of the first word) and the disc state flags, plus a log line with
 * the firmware variables that move between drive versions.
 */
void dvd_get_nr_debug(u32* progress, u32* state) {
	dvd_unlock();
	*progress = dvd_readmem_32(0x40ead0);
	*state = dvd_readmem_32(0x40ed00);

	// Firmware variables that move between versions (word aligned):
	// protection flags, command state, sense code
	u32 date = dvd_drive_date();
	u32 flags = 0x818c, cmd = 0x81a4, sense = 0x80d0;
	if (date == 0x20020402) {
		flags = 0x8198; cmd = 0x81b0; sense = 0x80dc;
	}
	else if (date == 0x20020823) {
		flags = 0x8194; cmd = 0x81ac; sense = 0x80d8;
	}
	else if (date == 0x20010831) {
		flags = 0x8190; cmd = 0x81a4; sense = 0x80d0;
	}
	dvd_unlock();
	print_gecko("NR drive %08X flags %08X cmd %08X sense %08X vector %08X mode %08X\r\n",
		date, dvd_readmem_32(flags), dvd_readmem_32(cmd), dvd_readmem_32(sense),
		dvd_readmem_32(0x804c), dvd_readmem_32(DRIVE_PATCH_ADDR + (DRIVE_PATCH_MODE_OFFSET & ~3)));
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
	if (nr_mode) {
		// Try the retail servo setup first and the NR
		// reader's if the disc doesn't initialise with it, then stick with
		// what worked for later re-inits during the dump.
		int servo = nr_servo >= 0 ? nr_servo : 0;
		int ret = dvd_enable_nr_patches(servo);
		if (ret == NR_UNSUPPORTED_DRIVE) {
			return ret;
		}
		if (ret && nr_servo < 0 && (dvd_get_error() >> 24) != 1) {
			DVD_LowReset(DVD_RESETHARD);
			usleep(1150000);
			DVD_LowReset(DVD_RESETHARD);
			dvd_unlock();
			servo = 1;
			ret = dvd_enable_nr_patches(servo);
		}
		if (!ret) {
			nr_servo = servo;
		}
	}
	else {
		dvd_readmem_array(0x415460, cached_bca, 64);
		dvd_read_id();
	}
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
#ifdef HW_DOL
	if (nr_mode && nr_corr_ready) {
		nr_descramble(dst, len, offset);
	}
#endif
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

