/**
 * CleanRip - main.c
 * Copyright (C) 2010 emu_kidid
 *
 * Main driving code behind the disc ripper
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
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sdcard/gcsd.h>
#include <sys/dir.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "gc_dvd.h"
#include "verify.h"
#include "main.h"
#include "crc32.h"
#include "sha1.h"
#include "md5.h"

static int dumpCounter = 0;
static char gameName[32];
static char internalName[512];
static char mountPath[512];
static char padNeedScan = 0;
int whichfb = 0;
int reboot = 0;
int verify_in_use = 0;
GXRModeObj *vmode = NULL;
u32 *xfb[2] = { NULL, NULL };
int options_map[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
const DISC_INTERFACE* slotA = &__io_gcsda;
const DISC_INTERFACE* slotB = &__io_gcsdb;

void check_exit_status() {
	if (reboot) {	// Reboot
		*(volatile unsigned long *)0xCC003024 = 0;
	}
}

u32 get_buttons_pressed() {
	u32 buttons = 0;

	if (padNeedScan) {
		PAD_ScanPads();
		padNeedScan = 0;
	}

	u16 gcPad = PAD_ButtonsDown(0);

	if (gcPad & PAD_BUTTON_B) {
		buttons |= PAD_BUTTON_B;
	}

	if (gcPad & PAD_BUTTON_A) {
		buttons |= PAD_BUTTON_A;
	}

	if (gcPad & PAD_BUTTON_LEFT) {
		buttons |= PAD_BUTTON_LEFT;
	}

	if (gcPad & PAD_BUTTON_RIGHT)  {
		buttons |= PAD_BUTTON_RIGHT;
	}

	if (gcPad & PAD_BUTTON_UP) {
		buttons |= PAD_BUTTON_UP;
	}

	if (gcPad & PAD_BUTTON_DOWN) {
		buttons |= PAD_BUTTON_DOWN;
	}

	if (gcPad & PAD_TRIGGER_Z) {
		reboot = 1;
	}
	check_exit_status();
	return buttons;
}

void wait_press_A() {
	// Draw the A button
	DrawAButton(265, 310);
	DrawFrameFinish();
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (!(get_buttons_pressed() & PAD_BUTTON_A));
}

void wait_press_A_exit_B() {
	// Draw the A and B buttons
	DrawAButton(195, 310);
	DrawBButton(390, 310);
	DrawFrameFinish();
	while ((get_buttons_pressed() & (PAD_BUTTON_A | PAD_BUTTON_B)));
	while (1) {
		while (!(get_buttons_pressed() & (PAD_BUTTON_A | PAD_BUTTON_B)));
		if (get_buttons_pressed() & PAD_BUTTON_A) {
			break;
		} else if (get_buttons_pressed() & PAD_BUTTON_B) {
			reboot = 1;
			check_exit_status();
		}
	}
}

static void InvalidatePADS() {
	padNeedScan = 1;
}

/* start up the Gamecube */
static void Initialise() {
	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	PAD_Init();
	
	// Obtain the preferred video mode from the system
	vmode = VIDEO_GetPreferredMode(NULL);

	// Set up the video registers with the chosen mode
	VIDEO_Configure(vmode);

	// Allocate memory for the display in the uncached region
	xfb[0] = (u32 *) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	xfb[1] = (u32 *) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(vmode, xfb[1], COLOR_BLACK);

	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb[0]);

	VIDEO_SetPostRetraceCallback(InvalidatePADS);

	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	init_font();
	whichfb = 0;
}

/* show the disclaimer */
static void show_disclaimer() {
	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	WriteCentre(190, "Disclaimer");
	WriteCentre(230, "The author is not responsible for any");
	WriteCentre(255, "damages that could occur to any");
	WriteCentre(280, "removable device used within this program");
	DrawFrameFinish();

	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	WriteCentre(190, "Disclaimer");
	WriteCentre(230, "The author is not responsible for any");
	WriteCentre(255, "damages that could occur to any");
	WriteCentre(280, "removable device used within this program");
	WriteCentre(315, "Press  A to continue  B to Exit");
	sleep(5);
	wait_press_A_exit_B();
}

/* Initialise the dvd drive + disc */
static int initialise_dvd() {
	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	WriteCentre(255, "Insert a GC DVD Disc");
	WriteCentre(315, "Press  A to continue  B to Exit");
	wait_press_A_exit_B();

	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	WriteCentre(255, "Initialising Disc ...");
	DrawFrameFinish();
	int ret = init_dvd();

	if (ret) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, dvd_error_str());
		DrawFrameFinish();
		sleep(3);
	}
	return ret;
}

/* Initialise the usb */
static int initialise_device(int slot) {
	int ret = 0;

	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	if (slot == TYPE_SLOTA) {
		WriteCentre(255, "Insert a SDGecko into slot A");
	} else {
		WriteCentre(255, "Insert a SDGecko into slot B");
	}
	WriteCentre(315, "Press  A to continue  B to Exit");
	wait_press_A_exit_B();

	if(slot == TYPE_SLOTA) {
		slotA->shutdown();
		slotA->startup();
	}
	else {
		slotB->shutdown();
		slotB->startup();
	}
	ret = fatMountSimple("fat", slot == TYPE_SLOTA ? slotA : slotB);
	sprintf(&mountPath[0], "fat:/");
	if (!ret) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "Error Mounting Device [%08X]", ret);
		WriteCentre(255, txtbuffer);
		WriteCentre(315, "Press A to try again  B to exit");
		wait_press_A_exit_B();
		return ret;
	}
	DIR* dp = opendir( &mountPath[0] );
	if(!dp) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "Error Reading Device [%08X]", ret);
		WriteCentre(255, txtbuffer);
		WriteCentre(315, "Press A to try again  B to exit");
		wait_press_A_exit_B();
		return ret;
	}
	closedir(dp);

	return ret;
}

/* if this is an unknown disc type, prompt the user and ask to continue */
static void identify_disc() {
	char *readbuf = (char*)memalign(32,2048);
	
	memset(&internalName[0],0,512);
	// Read the header
	dvd_read(readbuf, 2048, 0);
	if (readbuf[0]) {
		strncpy(&gameName[0], readbuf, 6);
		// Multi Disc identifier support
		if (readbuf[6]) {
			sprintf(&gameName[0], "%s-disc%i", &gameName[0],
					(readbuf[6]) + 1);
		}
		strncpy(&internalName[0],&readbuf[32],512);
	} else {
		sprintf(&gameName[0], "disc%i", dumpCounter);
	}
	if ((*(volatile unsigned int*) (readbuf+0x1C)) != NGC_MAGIC) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Unknown disc type!");
		WriteCentre(315, "Press  A to continue  B to Exit");
		wait_press_A_exit_B();
	}
	free(readbuf);
}

/* the user must specify the device slot */
static int device_slot() {
	int slot = TYPE_SLOTA;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Please select the device slot");
		DrawSelectableButton(100, 310, -1, 340, "Slot A",
				(slot == TYPE_SLOTA) ? B_SELECTED : B_NOSELECT);
		DrawSelectableButton(380, 310, -1, 340, "Slot B",
				(slot == TYPE_SLOTB) ? B_SELECTED : B_NOSELECT);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			slot ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			slot ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	return slot;
}

char *getShrinkOption() {
	int opt = options_map[NGC_SHRINK_ISO];
	if (opt == SHRINK_ALL)
		return "Shrink All";
	else if (opt == SHRINK_PAD_GARBAGE)
		return "Wipe Garbage";
	else if (opt == SHRINK_NONE)
		return "No";
	return 0;
}

char *getAlignOption() {
	int opt = options_map[NGC_ALIGN_FILES];
	if (opt == ALIGN_ALL)
		return "Align All";
	else if (opt == ALIGN_AUDIO)
		return "Audio Only";
	return 0;
}

char *getAlignmentBoundaryOption() {
	int opt = options_map[NGC_ALIGN_BOUNDARY];
	if (opt == ALIGN_32)
		return "32Kb";
	else if (opt == ALIGN_2)
		return "2KB";
	else if (opt == ALIGN_512)
		return "512B";
	return 0;
}

int getMaxPos(int option_pos) {
	switch (option_pos) {
	case NGC_ALIGN_BOUNDARY:
		return ALIGNB_DELIM;
	case NGC_ALIGN_FILES:
		return ALIGN_DELIM;
	case NGC_SHRINK_ISO:
		return SHRINK_DELIM;
	}
	return 0;
}

void toggleOption(int option_pos, int dir) {
	int max = getMaxPos(option_pos);
	if (options_map[option_pos] + dir >= max) {
		options_map[option_pos] = 0;
	} else if (options_map[option_pos] + dir < 0) {
		options_map[option_pos] = max - 1;
	} else {
		options_map[option_pos] += dir;
	}
}

static void get_settings() {
	int currentSettingPos = 0, maxSettingPos = MAX_NGC_OPTIONS - 1;

	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(75, 120, vmode->fbWidth - 78, 400, COLOR_BLACK);
		WriteCentre(130, "Gamecube Disc Ripper Setup:");

		// Gamecube Settings
		WriteFont(80, 160 + (32* 1 ), "Shrink ISO");
		DrawSelectableButton(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, getShrinkOption(), (!currentSettingPos) ? B_SELECTED:B_NOSELECT);
   		WriteFont(80, 160+(32*2), "Align Files");
   		DrawSelectableButton(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, getAlignOption(), (currentSettingPos==1) ? B_SELECTED:B_NOSELECT);
   		WriteFont(80, 160+(32*3), "Alignment boundary");
   		DrawSelectableButton(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, getAlignmentBoundaryOption(), (currentSettingPos==2) ? B_SELECTED:B_NOSELECT);
   		WriteCentre(370,"Press  A  to continue");
		DrawAButton(265,360);
		DrawFrameFinish();

		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT | PAD_BUTTON_A | PAD_BUTTON_UP | PAD_BUTTON_DOWN)));
		u32 btns = get_buttons_pressed();
		if(btns & PAD_BUTTON_RIGHT) {
			toggleOption(currentSettingPos+0, 1);
		}
		if(btns & PAD_BUTTON_LEFT) {
			toggleOption(currentSettingPos+0, -1);
		}
		if(btns & PAD_BUTTON_UP) {
			currentSettingPos = (currentSettingPos>0) ? (currentSettingPos-1):maxSettingPos;
		}
		if(btns & PAD_BUTTON_DOWN) {
			currentSettingPos = (currentSettingPos<maxSettingPos) ? (currentSettingPos+1):0;
		}
		if(btns & PAD_BUTTON_A) {
			break;
		}
		while (get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT | PAD_BUTTON_A | PAD_BUTTON_UP | PAD_BUTTON_DOWN));
	}
	while(get_buttons_pressed() & PAD_BUTTON_B);
}

void dump_info(char *md5, char *sha1, u32 crc32, int verified) {
	char infoLine[1024];
	memset(infoLine, 0, 1024);
	sprintf(infoLine, "--File Generated by CleanRip Gamecube Edition v%i.%i.%i--"
					  "\r\n\r\nFilename: %s\r\nInternal Name: %s\r\nMD5: %s\r\n"
					  "SHA-1: %s\r\nCRC32: %08X\r\nVersion: 1.0%i\r\nVerified: %s\r\n", 
			V_MAJOR,V_MID,V_MINOR,&gameName[0],&internalName[0], md5, sha1, crc32, *(u8*)0x80000007,
			verified ? "Yes" : "No");
	sprintf(txtbuffer, "%s%s-dumpinfo.txt", &mountPath[0], &gameName[0]);
	remove(&txtbuffer[0]);
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		fwrite(infoLine, 1, strlen(&infoLine[0]), fp);
		fclose(fp);
	}
}

void dump_game() {

	md5_state_t state;
	md5_byte_t digest[16];
	SHA1Context sha;
	u32 crc32 = 0;

	u32 previousLBA = 0;
	u32 startLBA = 0;
	u32 endLBA = NGC_DISC_SIZE;

	// Reset MD5/SHA-1/CRC
	md5_init(&state);
	SHA1Reset(&sha);
	crc32 = 0;

	// There will be chunks, name accordingly
	sprintf(txtbuffer, "%s%s.iso", &mountPath[0], &gameName[0]);
	
	remove(&txtbuffer[0]);
	FILE *fp = fopen(&txtbuffer[0], "wb");
	if (fp == NULL) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(230, "Failed to create file:");
		WriteCentre(255, txtbuffer);
		WriteCentre(315, "Exiting in 5 seconds");
		DrawFrameFinish();
		sleep(5);
		exit(0);
	}

	int ret = 0;
	long long copyTime = gettime();
	long long startTime = gettime();
	char* buffer = (char*)memalign(32,OPT_READ_SIZE);
	while (!ret && (startLBA + OPT_READ_SIZE) < endLBA) {
		// Read from Disc
		ret = dvd_read(buffer, (u32) (OPT_READ_SIZE << 11),
				(startLBA) << 11);
		// Calcaulte MD5
		md5_append(&state, (const md5_byte_t *) buffer, (u32) (OPT_READ_SIZE << 11));
		// Calculate SHA-1
		SHA1Input(&sha, (const unsigned char *) buffer, (u32) (OPT_READ_SIZE << 11));
		// Calculate CRC32
		crc32 = Crc32_ComputeBuf( crc32, (const void*) buffer, (u32) (OPT_READ_SIZE	<< 11));

		int bytes_written = fwrite(buffer, 1, OPT_READ_SIZE << 11, fp);
		if (bytes_written != (u32) (OPT_READ_SIZE << 11)) {
			fclose(fp);
			DrawFrameStart();
			DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
			sprintf(txtbuffer,"Write Error! %08x %08x",bytes_written,OPT_READ_SIZE << 11);
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Exiting in 10 seconds");
			DrawFrameFinish();
			sleep(10);
			reboot=1;
		}
		check_exit_status();

		if (get_buttons_pressed() & PAD_BUTTON_B) {
			ret = -61;
		}
		// Update status every second
		int timePassed = diff_msec(copyTime, gettime());
		if (timePassed > 2000) {
			int msecPerRead = (((startLBA - previousLBA) << 11) / timePassed);
			u64 remainder = (endLBA - startLBA);
			u32 etaTime = (remainder / msecPerRead) * timePassed;
			sprintf(txtbuffer, "%dMb %ikb/s - ETA %02d:%02d:%02d",
					(startLBA << 11) / (1024* 1024), 
  		        (int)(msecPerRead),
  		        (int)(((etaTime/1000)/60/60)%60),(int)(((etaTime/1000)/60)%60),(int)((etaTime/1000)%60));
					DrawFrameStart();
					DrawProgressBar((int)((float)((float)startLBA/(float)endLBA)*100), txtbuffer);
      		DrawFrameFinish();
  			previousLBA = startLBA;
  			copyTime = gettime();
		}
			startLBA+=OPT_READ_SIZE;
	}
	// Remainder of data
	if(!ret && startLBA < endLBA) {
		ret = dvd_read(buffer, (u32)((endLBA-startLBA)<<11), startLBA<<11);
		// Calculate MD5
		md5_append(&state, (const md5_byte_t *)buffer, (u32)((endLBA-startLBA)<<11));
		// Calculate SHA-1
		SHA1Input(&sha, (const unsigned char *) buffer, (u32)((endLBA-startLBA)<<11));
		// Calculate CRC32
		crc32 = Crc32_ComputeBuf( crc32, (const void*) buffer, (u32)((endLBA-startLBA)<<11));
		int bytes_written = fwrite(buffer, 1, (u32)((endLBA-startLBA)<<11), fp);
		if(bytes_written != (u32)((endLBA-startLBA)<<11)) {
			fclose(fp);
			DrawFrameStart();
			DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
			WriteCentre(255,"Write Error!");
			WriteCentre(315,"Exiting in 10 seconds");
			DrawFrameFinish();
			sleep(10);
			reboot=1;
			check_exit_status();
		}
	}
	md5_finish(&state, digest);
	fflush(fp);
	fclose(fp);
	
	if(ret != -61 && ret) {
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "%s",dvd_error_str());
		WriteCentre(255,txtbuffer);
		WriteCentre(315,"Press  A  to continue");
		dvd_motor_off();
		wait_press_A();
	}
	else if (ret == -61) {
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "Copy Cancelled");
		WriteCentre(255,txtbuffer);
		WriteCentre(315,"Press  A  to continue");
		dvd_motor_off();
		wait_press_A();
	}
	else {
		sprintf(txtbuffer,"Copy completed in %d mins. Press A",diff_sec(startTime, gettime())/60);
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		WriteCentre(190,txtbuffer);
		char md5sum[64];
		char sha1sum[64];
		memset(&md5sum[0], 0, 64);
		memset(&sha1sum[0], 0, 64);
		int i; for (i=0; i<16; i++) sprintf(&md5sum[0],"%s%02X",&md5sum[0],digest[i]);
		if(SHA1Result(&sha)) {
			for (i=0; i<5; i++) sprintf(&sha1sum[0],"%s%08X",&sha1sum[0],sha.Message_Digest[i]);
		}
		else {
			sprintf(sha1sum, "Error computing SHA-1");
		}
		int verified = (verify_is_available() && verify_findMD5Sum(&md5sum[0]));
		sprintf(txtbuffer, "MD5: %s", verified ? "Verified OK" : "");
		WriteCentre(230,txtbuffer);
		WriteCentre(255,verified ? verify_get_name() : "Not Verified with redump.org");
		WriteCentre(280,&md5sum[0]);
		dump_info(&md5sum[0], &sha1sum[0], crc32, verified);
		WriteCentre(315,"Press  A to continue  B to Exit");
		dvd_motor_off();
		wait_press_A_exit_B();
	}
	free(buffer);
}

int main(int argc, char **argv) {

	Initialise();
	show_disclaimer();

	while (1) {
		int slot = device_slot();
		int ret = -1;
		do {
			ret = initialise_device(slot);
		} while (ret != 1);

		// Try to load up redump.org dat files
		verify_init(&mountPath[0]);
		
		// Init the drive and try to detect disc type
		do {
			ret = initialise_dvd();
		} while (ret != 0);
		
		identify_disc();
		//get_settings();
		
		verify_in_use = verify_is_available();

		dump_game();

		verify_in_use = 0;
		dumpCounter++;
	}

	return 0;
}
