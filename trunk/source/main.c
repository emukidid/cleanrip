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
#include <di/di.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>
#include <ntfs.h>
#include <fat.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "gc_dvd.h"
#include "verify.h"
#include "main.h"
#include "md5.h"

static ntfs_md *mounts = NULL;
static int dumpCounter = 0;
static char gameName[32];
static char mountPath[512];
static char rawNTFSMount[512];
static char wpadNeedScan = 0;
static char padNeedScan = 0;
int shutdown = 0;
int whichfb = 0;
u32 iosversion = -1;
int verify_in_use = 0;
int verify_disc_type = 0;
GXRModeObj *vmode = NULL;
u32 *xfb[2] = { NULL, NULL };
int options_map[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
const DISC_INTERFACE* frontsd = &__io_wiisd;
const DISC_INTERFACE* usb = &__io_usbstorage;

void check_exit_status() {
	if (shutdown == 1) {//Power off System
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	}
	if (shutdown == 2) { //Return to HBC/whatever
		DI_Close();
		void (*rld)() = (void(*)()) 0x80001800;
		rld();
	}
}

u32 get_buttons_pressed() {
	WPADData *wiiPad;
	u32 buttons = 0;

	if (padNeedScan) {
		PAD_ScanPads();
		padNeedScan = 0;
	}
	if (wpadNeedScan) {
		WPAD_ScanPads();
		wpadNeedScan = 0;
	}

	u16 gcPad = PAD_ButtonsDown(0);
	wiiPad = WPAD_Data(0);

	if ((gcPad & PAD_BUTTON_B) || (wiiPad->btns_h & WPAD_BUTTON_B)) {
		buttons |= PAD_BUTTON_B;
	}

	if ((gcPad & PAD_BUTTON_A) || (wiiPad->btns_h & WPAD_BUTTON_A)) {
		buttons |= PAD_BUTTON_A;
	}

	if ((gcPad & PAD_BUTTON_LEFT) || (wiiPad->btns_h & WPAD_BUTTON_LEFT)) {
		buttons |= PAD_BUTTON_LEFT;
	}

	if ((gcPad & PAD_BUTTON_RIGHT) || (wiiPad->btns_h & WPAD_BUTTON_RIGHT)) {
		buttons |= PAD_BUTTON_RIGHT;
	}

	if ((gcPad & PAD_BUTTON_UP) || (wiiPad->btns_h & WPAD_BUTTON_UP)) {
		buttons |= PAD_BUTTON_UP;
	}

	if ((gcPad & PAD_BUTTON_DOWN) || (wiiPad->btns_h & WPAD_BUTTON_DOWN)) {
		buttons |= PAD_BUTTON_DOWN;
	}

	if ((gcPad & PAD_TRIGGER_Z) || (wiiPad->btns_h & WPAD_BUTTON_HOME)) {
		shutdown = 2;
	}
	check_exit_status();
	return buttons;
}

void wait_press_A() {
	// Draw the A button
	DrawAButton(265, 310);
	DrawFrameFinish();
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	while (!(get_buttons_pressed() & PAD_BUTTON_A))
		;
}

void wait_press_A_exit_B() {
	// Draw the A and B buttons
	DrawAButton(195, 310);
	DrawBButton(390, 310);
	DrawFrameFinish();
	while ((get_buttons_pressed() & (PAD_BUTTON_A | PAD_BUTTON_B)))
		;
	while (1) {
		while (!(get_buttons_pressed() & (PAD_BUTTON_A | PAD_BUTTON_B)))
			;
		if (get_buttons_pressed() & PAD_BUTTON_A) {
			break;
		} else if (get_buttons_pressed() & PAD_BUTTON_B) {
			exit(0);
		}
	}
}

static void InvalidatePADS() {
	padNeedScan = wpadNeedScan = 1;
}

/* check for ahbprot */
static int have_hw_access() {
	if ((*(volatile unsigned int*) HW_ARMIRQMASK)
			&& (*(volatile unsigned int*) HW_ARMIRQFLAG)) {
		return 1;
	}
	return 0;
}

void ShutdownWii() {
	shutdown = 1;
}

/* start up the Wii */
static void Initialise() {
	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	PAD_Init();
	CONF_Init();
	WPAD_Init();
	WPAD_SetIdleTimeout(120);
	WPAD_SetPowerButtonCallback((WPADShutdownCallback) ShutdownWii);
	SYS_SetPowerCallback(ShutdownWii);

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
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

/* FindIOS - borrwed from Tantric */
static int FindIOS(u32 ios) {
	s32 ret;
	u32 n;

	u64 *titles = NULL;
	u32 num_titles = 0;

	ret = ES_GetNumTitles(&num_titles);
	if (ret < 0)
		return 0;

	if (num_titles < 1)
		return 0;

	titles = (u64 *) memalign(32, num_titles * sizeof(u64) + 32);
	if (!titles)
		return 0;

	ret = ES_GetTitles(titles, num_titles);
	if (ret < 0) {
		free(titles);
		return 0;
	}

	for (n = 0; n < num_titles; n++) {
		if ((titles[n] & 0xFFFFFFFF) == ios) {
			free(titles);
			return 1;
		}
	}
	free(titles);
	return 0;
}

/* check for AHBPROT & IOS58 */
static void hardware_checks() {
	if (!have_hw_access()) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(190, "AHBPROT check failed");
		WriteCentre(255, "Please install the latest HBC");
		WriteCentre(280, "Check the FAQ for more info");
		WriteCentre(315, "Press A to Exit");
		wait_press_A();
		exit(0);
	}

	int ios58exists = FindIOS(58);
	if (ios58exists && iosversion != 58) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(190, "IOS Version check failed");
		WriteCentre(255, "IOS 58 exists but is not in use");
		WriteCentre(280, "Dumping to USB will be SLOW!");
		WriteCentre(315, "Press  A to continue  B to Exit");
		wait_press_A_exit_B();
	}
	if (!ios58exists) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(190, "IOS Version check failed");
		WriteCentre(255, "Please install IOS58");
		WriteCentre(280, "Dumping to USB will be SLOW!");
		WriteCentre(315, "Press  A to continue  B to Exit");
		wait_press_A_exit_B();
	}
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
	WriteCentre(255, "Insert a GC/Wii DVD Disc");
	WriteCentre(315, "Press  A to continue  B to Exit");
	wait_press_A_exit_B();

	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	WriteCentre(255, "Initialising Disc ...");
	DrawFrameFinish();
	int ret = init_dvd();

	if (ret == NO_DISC) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "No disc detected");
		DrawFrameFinish();
		sleep(3);
	}
	return ret;
}

/* Initialise the usb */
static int initialise_device(int type, int fs) {
	int ret = 0;

	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
	if (type == TYPE_USB) {
		WriteCentre(255, "Insert a USB FAT32/NTFS formatted device");
	} else {
		WriteCentre(255, "Insert a SD FAT32 formatted device");
	}
	WriteCentre(315, "Press  A to continue  B to Exit");
	wait_press_A_exit_B();

	if (fs == TYPE_FAT) {
		ret = fatMountSimple("fat", type == TYPE_USB ? usb : frontsd);
		sprintf(&mountPath[0], "fat:/");
		if (ret != 1) {
			DrawFrameStart();
			DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
			sprintf(txtbuffer, "Error Mounting Device [%08X]", ret);
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Press A to try again  B to exit");
			wait_press_A_exit_B();
		}
	} else if (fs == TYPE_NTFS) {
		fatInitDefault();
		int mountCount = ntfsMountDevice(usb, &mounts, (NTFS_DEFAULT
				| NTFS_RECOVER) | (NTFS_SU));
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		if (!mountCount || mountCount == -1) {
			if (mountCount == -1) {
				sprintf(txtbuffer, "Error whilst mounting devices (%i)", errno);
			} else {
				sprintf(txtbuffer, "No NTFS volumes were found and/or mounted");
			}
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Press A to try again  B to exit");
			wait_press_A_exit_B();
		} else {
			sprintf(txtbuffer, "%s Mounted", ntfsGetVolumeName(mounts[0].name));
			WriteCentre(230, txtbuffer);
			sprintf(txtbuffer, "%i NTFS volumes(s) mounted!", mountCount);
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Press  A  to continue");
			wait_press_A();
			sprintf(&mountPath[0], "%s:/", mounts[0].name);
			sprintf(&rawNTFSMount[0], "%s", mounts[0].name);
			ret = 1;
		}
	}
	return ret;
}

/* identify whether this disc is a Gamecube or Wii disc */
static int identify_disc() {
	char *readbuf = (char*)READ_BUFFER;

	// Read the header
	DVD_LowRead64(readbuf, 2048, 0ULL);
	if (readbuf[0]) {
		strncpy(&gameName[0], readbuf, 6);
		// Multi Disc identifier support
		if (readbuf[6]) {
			sprintf(&gameName[0], "%s-disc%i", &gameName[0],
					(readbuf[6]) + 1);
		}
	} else {
		sprintf(&gameName[0], "disc%i", dumpCounter);
	}
	if ((*(volatile unsigned int*) (readbuf+0x1C)) == NGC_MAGIC) {
		return IS_NGC_DISC;
	}
	if ((*(volatile unsigned int*) (readbuf+0x18)) == WII_MAGIC) {
		return IS_WII_DISC;
	} else {
		return IS_UNK_DISC;
	}
}

/* the user must specify the disc type */
static int force_disc() {
	int type = IS_NGC_DISC;
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(190, "Failed to detect the disc type");
		WriteCentre(255, "Please select the correct type");
		DrawSelectableButton(100, 310, -1, 340, "Gamecube", (type
				== IS_NGC_DISC) ? B_SELECTED : B_NOSELECT);
		DrawSelectableButton(380, 310, -1, 340, "Wii",
				(type == IS_WII_DISC) ? B_SELECTED : B_NOSELECT);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			type ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	return type;
}

/* the user must specify the device type */
static int device_type() {
	int type = TYPE_USB;
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Please select the device type");
		DrawSelectableButton(100, 310, -1, 340, "USB",
				(type == TYPE_USB) ? B_SELECTED : B_NOSELECT);
		DrawSelectableButton(380, 310, -1, 340, "Front SD",
				(type == TYPE_SD) ? B_SELECTED : B_NOSELECT);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			type ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	return type;
}

/* the user must specify the file system type */
static int filesystem_type() {
	int type = TYPE_FAT;
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Please select the filesystem type");
		DrawSelectableButton(100, 310, -1, 340, "FAT",
				(type == TYPE_FAT) ? B_SELECTED : B_NOSELECT);
		DrawSelectableButton(380, 310, -1, 340, "NTFS",
				(type == TYPE_NTFS) ? B_SELECTED : B_NOSELECT);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			type ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	return type;
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

char *getDualLayerOption() {
	int opt = options_map[WII_DUAL_LAYER];
	if (opt == SINGLE_LAYER)
		return "No";
	else if (opt == DUAL_LAYER)
		return "Yes";
	return 0;
}

char *getNewFileOption() {
	int opt = options_map[WII_NEWFILE];
	if (opt == ASK_USER)
		return "Yes";
	else if (opt == AUTO_CHUNK)
		return "No";
	return 0;
}

char *getChunkSizeOption() {
	int opt = options_map[WII_CHUNK_SIZE];
	if (opt == CHUNK_1GB)
		return "1GB";
	else if (opt == CHUNK_2GB)
		return "2GB";
	else if (opt == CHUNK_3GB)
		return "3GB";
	else if (opt == CHUNK_MAX)
		return "Max";
	return 0;
}

int getMaxPos(int option_pos) {
	switch (option_pos) {
	case WII_DUAL_LAYER:
		return DUAL_DELIM;
	case WII_CHUNK_SIZE:
		return CHUNK_DELIM;
	case NGC_ALIGN_BOUNDARY:
		return ALIGNB_DELIM;
	case NGC_ALIGN_FILES:
		return ALIGN_DELIM;
	case NGC_SHRINK_ISO:
		return SHRINK_DELIM;
	case WII_NEWFILE:
		return NEWFILE_DELIM;
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

static void get_settings(int disc_type) {
	int currentSettingPos = 0, maxSettingPos =
			((disc_type == IS_WII_DISC) ? MAX_WII_OPTIONS : MAX_NGC_OPTIONS)
					- 1;

	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(75, 120, vmode->fbWidth - 78, 400, COLOR_BLACK);
		sprintf(txtbuffer, "%s Disc Ripper Setup:",
				disc_type == IS_WII_DISC ? "Wii" : "Gamecube");
		WriteCentre(130, txtbuffer);

		// Gamecube Settings
		if (disc_type == IS_NGC_DISC) {
			WriteFont(80, 160 + (32* 1 ), "Shrink ISO");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, getShrinkOption(), (!currentSettingPos) ? B_SELECTED:B_NOSELECT);
   		WriteFont(80, 160+(32*2), "Align Files");
   		DrawSelectableButton(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, getAlignOption(), (currentSettingPos==1) ? B_SELECTED:B_NOSELECT);
   		WriteFont(80, 160+(32*3), "Alignment boundary");
   		DrawSelectableButton(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, getAlignmentBoundaryOption(), (currentSettingPos==2) ? B_SELECTED:B_NOSELECT);
    }
			// Wii Settings
			else if(disc_type == IS_WII_DISC) {
				WriteFont(80, 160+(32*1), "Dual Layer");
				DrawSelectableButton(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, getDualLayerOption(), (!currentSettingPos) ? B_SELECTED:B_NOSELECT);
				WriteFont(80, 160+(32*2), "Chunk Size");
				DrawSelectableButton(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, getChunkSizeOption(), (currentSettingPos==1) ? B_SELECTED:B_NOSELECT);
				WriteFont(80, 160+(32*3), "New device per chunk");
				DrawSelectableButton(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, getNewFileOption(), (currentSettingPos==2) ? B_SELECTED:B_NOSELECT);
			}
			WriteCentre(370,"Press  A  to continue");
			DrawAButton(265,360);
			DrawFrameFinish();

			while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT | PAD_BUTTON_A | PAD_BUTTON_UP | PAD_BUTTON_DOWN)));
			u32 btns = get_buttons_pressed();
			if(btns & PAD_BUTTON_RIGHT) {
				toggleOption(currentSettingPos+((disc_type == IS_WII_DISC)?MAX_NGC_OPTIONS:0), 1);
			}
			if(btns & PAD_BUTTON_LEFT) {
				toggleOption(currentSettingPos+((disc_type == IS_WII_DISC)?MAX_NGC_OPTIONS:0), -1);
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

void prompt_new_file(FILE *fp, int chunk, int type, int fs, int silent) {
	// Close the file and unmount the fs
	fclose(fp);
	if(silent == ASK_USER) {
		if (fs == TYPE_FAT) {
			fatUnmount("fat");
			if (type == TYPE_SD) {
				frontsd->shutdown();
			} else if (type == TYPE_USB) {
				usb->shutdown();
			}
		}
		if (fs == TYPE_NTFS) {
			ntfsUnmount(&rawNTFSMount[0], true);
			free(mounts);
			usb->shutdown();
		}
		// Stop the disc if we're going to wait on the user
		dvd_motor_off();
	}

	if(silent == ASK_USER) {
		int ret = -1;
		do {
				DrawFrameStart();
				DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
				WriteCentre(255, "Insert a device for the next chunk");
				WriteCentre(315, "Press  A to continue  B to Exit");
				wait_press_A_exit_B();
	
			if (fs == TYPE_FAT) {
				int i = 0;
				for (i = 0; i < 10; i++) {
					ret = fatMountSimple("fat", type == TYPE_USB ? usb : frontsd);
					if (ret == 1) {
						break;
					}
				}
			} else if (fs == TYPE_NTFS) {
				fatInitDefault();
				ntfs_md *mounts = NULL;
				int mountCount = ntfsMountDevice(usb, &mounts, (NTFS_DEFAULT
						| NTFS_RECOVER) | (NTFS_SU));
				if (mountCount && mountCount != -1) {
					sprintf(&mountPath[0], "%s:/", mounts[0].name);
					ret = 1;
				} else {
					ret = -1;
				}
			}
			if (ret != 1) {
				DrawFrameStart();
				DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
				sprintf(txtbuffer, "Error Mounting Device [%08X]", ret);
				WriteCentre(255, txtbuffer);
				WriteCentre(315, "Press A to try again  B to exit");
				wait_press_A_exit_B();
			}
		} while (ret != 1);
	}

	fp = NULL;
	sprintf(txtbuffer, "%s%s.part%i.iso", &mountPath[0], &gameName[0], chunk);
	fp = fopen(&txtbuffer[0], "wb");
	if (fp == NULL) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Failed to create file!");
		WriteCentre(315, "Exiting in 5 seconds");
		DrawFrameFinish();
		sleep(5);
		exit(0);
	}
	if(silent == ASK_USER) {
		init_dvd();
	}
}

void dump_bca() {
	sprintf(txtbuffer, "%s%s.bca", &mountPath[0], &gameName[0]);
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		char* bca_data = (char*) READ_BUFFER;
		memset(bca_data, 0, 64);
		DI_Read_BCA(bca_data);
		fwrite(bca_data, 1, 0x40, fp);
		fclose(fp);
	}
}

void dump_md5(char *txt) {
	char md5line[1024];
	memset(md5line, 0, 1024);
	sprintf(md5line, "%s MD5: %s\n", &gameName[0], txt);
	sprintf(txtbuffer, "%s%s.md5", &mountPath[0], &gameName[0]);
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		fwrite(md5line, 1, strlen(&md5line[0]), fp);
		fclose(fp);
	}
}

void dump_game(int disc_type, int type, int fs) {

	md5_state_t state;
	md5_byte_t digest[16];

	// Check if we will ask the user to insert a new device per chunk
	int silent = options_map[WII_NEWFILE];
	
	// The read size
	int opt_read_size = ONE_MEGABYTE;

	u32 previousLBA = 0;
	u32 startLBA = 0;
	u32 endLBA = disc_type == IS_NGC_DISC ? NGC_DISC_SIZE
			: (options_map[WII_DUAL_LAYER] == DUAL_LAYER ? WII_D9_SIZE
					: WII_D5_SIZE);

	// Work out the chunk size
	u32 chunk_size_wii = options_map[WII_CHUNK_SIZE];
	u32 opt_chunk_size = chunk_size_wii == CHUNK_MAX ? (endLBA + opt_read_size)
			: ((chunk_size_wii + 1) * ONE_GIGABYTE);

	if (disc_type == IS_NGC_DISC) {
		opt_chunk_size = NGC_DISC_SIZE;
	}

	// Dump the BCA for Nintendo discs
	if (disc_type == IS_WII_DISC || disc_type == IS_NGC_DISC) {
		dump_bca();
	}

	// Create the read buffer
	char *buffer = (char*) READ_BUFFER;

	md5_init(&state);

	// There will be chunks, name accordingly
	if (opt_chunk_size < endLBA) {
		sprintf(txtbuffer, "%s%s.part0.iso", &mountPath[0], &gameName[0]);
	} else {
		sprintf(txtbuffer, "%s%s.iso", &mountPath[0], &gameName[0]);
	}
	FILE *fp = fopen(&txtbuffer[0], "wb");
	if (fp == NULL) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "%schunk0.bin", &mountPath[0]);
		WriteCentre(230, txtbuffer);
		WriteCentre(255, "Failed to create file!");
		WriteCentre(315, "Exiting in 5 seconds");
		DrawFrameFinish();
		sleep(5);
		exit(0);
	}

	int ret = 0;
	long long copyTime = gettime();
	long long startTime = gettime();
	int chunk = 1;

	while (!ret && (startLBA + opt_read_size) < endLBA) {
		if (startLBA > (opt_chunk_size * chunk)) {
			prompt_new_file(fp, chunk, type, fs, silent);
			chunk++;
		}

		ret = DVD_LowRead64(buffer, (u32) (opt_read_size << 11),
				(u64) (((u64) startLBA) << 11));
		md5_append(&state, (const md5_byte_t *) buffer, (u32) (opt_read_size
				<< 11));

		int bytes_written = fwrite(buffer, 1, (u32) (opt_read_size << 11), fp);
		if (bytes_written != (u32) (opt_read_size << 11)) {
			fclose(fp);
			DrawFrameStart();
			DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
			WriteCentre(255, "Write Error!");
			WriteCentre(315, "Exiting in 10 seconds");
			DrawFrameFinish();
			sleep(10);
			exit(1);
		}
		check_exit_status();

		if (get_buttons_pressed() & PAD_BUTTON_B) {
			ret = -61;
		}
		// Update status every second
		int timePassed = diff_msec(copyTime, gettime());
		if (timePassed > 1000) {
			int msecPerRead = (((startLBA - previousLBA) << 11) / timePassed);
			u64 remainder = (endLBA - startLBA);
			u32 etaTime = (remainder / msecPerRead) * timePassed;
			sprintf(txtbuffer, "%dMb %4.0fkb/s - ETA %02d:%02d:%02d",
					(int) (((u64) ((u64) startLBA << 11)) / (1024* 1024 )), 
  		        (float)(msecPerRead),
  		        (int)(((etaTime/1000)/60/60)%60),(int)(((etaTime/1000)/60)%60),(int)((etaTime/1000)%60));
					DrawFrameStart();
					DrawProgressBar((int)((float)((float)startLBA/(float)endLBA)*100), txtbuffer);
      DrawFrameFinish();
  		previousLBA = startLBA;
  		copyTime = gettime();
		}

			startLBA+=opt_read_size;

		}
	// Remainder of data
	if(!ret && startLBA < endLBA) {
		ret = DVD_LowRead64(buffer, (u32)((endLBA-startLBA)<<11), (u64)((u64)startLBA<<11));
		md5_append(&state, (const md5_byte_t *)buffer, (u32)((endLBA-startLBA)<<11));
		int bytes_written = fwrite(buffer, 1, (u32)((endLBA-startLBA)<<11), fp);
		if(bytes_written != (u32)((endLBA-startLBA)<<11)) {
			fclose(fp);
			DrawFrameStart();
			DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
			WriteCentre(255,"Write Error!");
			WriteCentre(315,"Exiting in 10 seconds");
			DrawFrameFinish();
			sleep(10);
			exit(1);
		}
	}
	fclose(fp);
	md5_finish(&state, digest);
	if(ret != -61 && ret) {
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "%s",dvd_error_str());
		WriteCentre(255,txtbuffer);
		WriteCentre(315,"Press  A  to continue");
		wait_press_A();
	}
	else if (ret == -61) {
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		sprintf(txtbuffer, "Copy Cancelled");
		WriteCentre(255,txtbuffer);
		WriteCentre(315,"Press  A  to continue");
		wait_press_A();
	}
	else {
		sprintf(txtbuffer,"Copy completed in %d mins. Press A",diff_sec(startTime, gettime())/60);
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		WriteCentre(190,txtbuffer);
		char md5sum[32];
		memset(&md5sum[0], 0, 32);
		int i; for (i=0; i<16; i++) sprintf(&md5sum[0],"%s%02X",&md5sum[0],digest[i]);
		int verified = (verify_is_available(disc_type) && verify_findMD5Sum(&md5sum[0], disc_type));
		sprintf(txtbuffer, "MD5: %s", verified ? "Verified OK" : "");
		WriteCentre(230,txtbuffer);
		WriteCentre(255,verified ? verify_get_name() : "Not Verified with redump.org");
		WriteCentre(280,&md5sum[0]);
		dump_md5(&md5sum[0]);
		WriteCentre(315,"Press  A to continue  B to Exit");
		wait_press_A_exit_B();
	}
	dvd_motor_off();
}

int main(int argc, char **argv) {

	Initialise();
	iosversion = IOS_GetVersion();

	show_disclaimer();
	hardware_checks();

	while (1) {
		int type = device_type();
		int fs = TYPE_FAT;

		if (type == TYPE_USB) {
			fs = filesystem_type();
		}

		int ret = -1;
		do {
			ret = initialise_device(type, fs);
		} while (ret != 1);

		// Try to load up redump.org dat files
		verify_init(&mountPath[0]);
		
		// Init the drive and try to detect disc type
		ret = NO_DISC;
		do {
			ret = initialise_dvd();
		} while (ret == NO_DISC);

		int disc_type = identify_disc();

		if (disc_type == IS_UNK_DISC) {
			disc_type = force_disc();
		}

		if (disc_type == IS_WII_DISC) {
			get_settings(disc_type);
		}

		verify_in_use = verify_is_available(disc_type);
		verify_disc_type = disc_type;
		
		dump_game(disc_type, type, fs);

		verify_in_use = 0;
		dumpCounter++;
	}

	return 0;
}
