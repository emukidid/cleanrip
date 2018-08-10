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
#include <malloc.h>
#include <stdarg.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "gc_dvd.h"
#include "verify.h"
#include "main.h"
#include "crc32.h"
#include "sha1.h"
#include "md5.h"
#include <fat.h>

#define DEFAULT_FIFO_SIZE    (256*1024)//(64*1024) minimum

#ifdef HW_RVL
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>
#include <ntfs.h>
#endif

#ifdef HW_RVL
static ntfs_md *mounts = NULL;
const DISC_INTERFACE* sdcard = &__io_wiisd;
const DISC_INTERFACE* usb = &__io_usbstorage;
static char rawNTFSMount[512];
#endif
#ifdef HW_DOL
#include <sdcard/gcsd.h>
const DISC_INTERFACE* sdcard = &__io_gcsda;
const DISC_INTERFACE* usb = NULL;
#endif

static int calcChecksums = 0;
static int dumpCounter = 0;
static char gameName[32];
static char internalName[512];
static char mountPath[512];
static char wpadNeedScan = 0;
static char padNeedScan = 0;
int print_usb = 0;
int shutdown = 0;
int whichfb = 0;
u32 iosversion = -1;
int verify_in_use = 0;
int verify_disc_type = 0;
GXRModeObj *vmode = NULL;
u32 *xfb[2] = { NULL, NULL };
int options_map[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

enum {
	MSG_SETFILE,
	MSG_WRITE,
	MSG_FLUSH,
};

typedef union _writer_msg {
	struct {
		int command;
		void* data;
		u32 length;
		mqbox_t ret_box;
	};
	uint8_t pad[32]; // pad to 32 bytes for alignment
} writer_msg;

static void* writer_thread(void* _msgq) {
	FILE* fp = NULL;
	mqbox_t msgq = (mqbox_t)_msgq;
	writer_msg* msg;

	// stupid libogc returns TRUE even if the message queue gets destroyed while waiting
	while (MQ_Receive(msgq, (mqmsg_t*)&msg, MQ_MSG_BLOCK)==TRUE && msg) {
		switch (msg->command) {
			case MSG_SETFILE:
				fp = (FILE*)msg->data;
				break;
			case MSG_WRITE:
				if (fp && fwrite(msg->data, msg->length, 1, fp)!=1) {
					// write error, signal it by pushing a NULL message to the front
					MQ_Jam(msg->ret_box, (mqmsg_t)NULL, MQ_MSG_BLOCK);
					return NULL;
				}

				// release the block so it can be reused
				MQ_Send(msg->ret_box, (mqmsg_t)msg, MQ_MSG_BLOCK);
				break;
			case MSG_FLUSH:
				*(vu32*)msg->data = 1;
				break;
		}
	}

	return msg;
}


void print_gecko(const char* fmt, ...)
{
	if(print_usb) {
		char tempstr[2048];
		va_list arglist;
		va_start(arglist, fmt);
		vsprintf(tempstr, fmt, arglist);
		va_end(arglist);
		usb_sendbuffer_safe(1,tempstr,strlen(tempstr));
	}
}


void check_exit_status() {
#ifdef HW_DOL
	if(shutdown == 1 || shutdown == 2)
		exit(0);
#endif
#ifdef HW_RVL
	if (shutdown == 1) {//Power off System
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	}
	if (shutdown == 2) { //Return to HBC/whatever
		void (*rld)() = (void(*)()) 0x80001800;
		rld();
	}
#endif
}

#ifdef HW_RVL
u32 get_wii_buttons_pressed(u32 buttons) {
	WPADData *wiiPad;
	if (wpadNeedScan) {
		WPAD_ScanPads();
		wpadNeedScan = 0;
	}
	wiiPad = WPAD_Data(0);
	
	if (wiiPad->btns_h & WPAD_BUTTON_B) {
		buttons |= PAD_BUTTON_B;
	}

	if (wiiPad->btns_h & WPAD_BUTTON_A) {
		buttons |= PAD_BUTTON_A;
	}

	if (wiiPad->btns_h & WPAD_BUTTON_LEFT) {
		buttons |= PAD_BUTTON_LEFT;
	}

	if (wiiPad->btns_h & WPAD_BUTTON_RIGHT) {
		buttons |= PAD_BUTTON_RIGHT;
	}

	if (wiiPad->btns_h & WPAD_BUTTON_UP) {
		buttons |= PAD_BUTTON_UP;
	}

	if (wiiPad->btns_h & WPAD_BUTTON_DOWN) {
		buttons |= PAD_BUTTON_DOWN;
	}

	if (wiiPad->btns_h & WPAD_BUTTON_HOME) {
		shutdown = 2;
	}
	return buttons;
}
#endif

u32 get_buttons_pressed() {
	u32 buttons = 0;

	if (padNeedScan) {
		PAD_ScanPads();
		padNeedScan = 0;
	}
	
#ifdef HW_RVL
	buttons = get_wii_buttons_pressed(buttons);
#endif

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

	if (gcPad & PAD_BUTTON_RIGHT) {
		buttons |= PAD_BUTTON_RIGHT;
	}

	if (gcPad & PAD_BUTTON_UP) {
		buttons |= PAD_BUTTON_UP;
	}

	if (gcPad & PAD_BUTTON_DOWN) {
		buttons |= PAD_BUTTON_DOWN;
	}

	if (gcPad & PAD_TRIGGER_Z) {
		shutdown = 2;
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
			exit(0);
		}
	}
}

static void InvalidatePADS() {
	padNeedScan = wpadNeedScan = 1;
}

/* check for ahbprot */
int have_hw_access() {
	if (read32(HW_ARMIRQMASK) && read32(HW_ARMIRQFLAG)) {
		// disable DVD irq for starlet
		mask32(HW_ARMIRQMASK, 1<<18, 0);
		print_gecko("AHBPROT access OK\r\n");
		return 1;
	}
	return 0;
}

void ShutdownWii() {
	shutdown = 1;
}

/* start up the GameCube/Wii */
static void Initialise() {
	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	PAD_Init();
#ifdef HW_RVL
	CONF_Init();
	WPAD_Init();
	WPAD_SetIdleTimeout(120);
	WPAD_SetPowerButtonCallback((WPADShutdownCallback) ShutdownWii);
	SYS_SetPowerCallback(ShutdownWii);
#endif

	vmode = VIDEO_GetPreferredMode(NULL);
	VIDEO_Configure(vmode);
	xfb[0] = (u32 *) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	xfb[1] = (u32 *) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(vmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetPostRetraceCallback(InvalidatePADS);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	// setup the fifo and then init GX
	void *gp_fifo = NULL;
	gp_fifo = MEM_K0_TO_K1 (memalign (32, DEFAULT_FIFO_SIZE));
	memset (gp_fifo, 0, DEFAULT_FIFO_SIZE);
	GX_Init (gp_fifo, DEFAULT_FIFO_SIZE);
	// clears the bg to color and clears the z buffer
	GX_SetCopyClear ((GXColor){0,0,0,255}, 0x00000000);
	// init viewport
	GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);
	// Set the correct y scaling for efb->xfb copy operation
	GX_SetDispCopyYScale ((f32) vmode->xfbHeight / (f32) vmode->efbHeight);
	GX_SetDispCopyDst (vmode->fbWidth, vmode->xfbHeight);
	GX_SetCullMode (GX_CULL_NONE); // default in rsp init
	GX_CopyDisp (xfb[0], GX_TRUE); // This clears the efb
	GX_CopyDisp (xfb[0], GX_TRUE); // This clears the xfb

	init_font();
	init_textures();
	whichfb = 0;
}

#ifdef HW_RVL
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
	print_gecko("IOS 58 Exists: %s\r\n", ios58exists ? "YES":"NO");
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
#endif

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
#ifdef HW_DOL
	WriteCentre(255, "Insert a GameCube DVD Disc");
#else	
	WriteCentre(255, "Insert a GC/Wii DVD Disc");
#endif
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

#ifdef HW_DOL
int select_slot() {
	int slot = 0;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Please select SDGecko Slot");
		DrawSelectableButton(100, 310, -1, 340, "Slot A", !slot ? B_SELECTED : B_NOSELECT, -1);
		DrawSelectableButton(380, 310, -1, 340, "Slot B", slot ? B_SELECTED : B_NOSELECT, -1);
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
#endif

/* Initialise the device */
static int initialise_device(int type, int fs) {
	int ret = 0;

	DrawFrameStart();
	DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
#ifdef HW_RVL
	if (type == TYPE_USB) {
		WriteCentre(255, "Insert a USB FAT32/NTFS formatted device");
	} 
	else 
#endif
	{
#ifdef HW_DOL
		sdcard = select_slot() ? &__io_gcsdb : &__io_gcsda;
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
#endif
		WriteCentre(255, "Insert a SD FAT32 formatted device");
	}
	WriteCentre(315, "Press  A to continue  B to Exit");
	wait_press_A_exit_B();

	if (fs == TYPE_FAT) {
		ret = fatMountSimple("fat", type == TYPE_USB ? usb : sdcard);
		sprintf(&mountPath[0], "fat:/");
		if (ret != 1) {
			DrawFrameStart();
			DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
			sprintf(txtbuffer, "Error Mounting Device [%08X]", ret);
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Press A to try again  B to exit");
			wait_press_A_exit_B();
		}
	} 
#ifdef HW_RVL
	else if (fs == TYPE_NTFS) {
		fatInitDefault();
		int mountCount = ntfsMountDevice(usb, &mounts, (NTFS_DEFAULT
				| NTFS_RECOVER) | (NTFS_SU));
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		if (!mountCount || mountCount == -1) {
			if (mountCount == -1) {
				sprintf(txtbuffer, "Error whilst mounting devices (%i)", errno);
			} else {
				sprintf(txtbuffer, "No NTFS volume(s) were found and/or mounted");
			}
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Press A to try again  B to exit");
			wait_press_A_exit_B();
		} else {
			sprintf(txtbuffer, "%s Mounted", ntfsGetVolumeName(mounts[0].name));
			WriteCentre(230, txtbuffer);
			sprintf(txtbuffer, "%i NTFS volume(s) mounted!", mountCount);
			WriteCentre(255, txtbuffer);
			WriteCentre(315, "Press  A  to continue");
			wait_press_A();
			sprintf(&mountPath[0], "%s:/", mounts[0].name);
			sprintf(&rawNTFSMount[0], "%s", mounts[0].name);
			ret = 1;
		}
	}
#endif
	return ret;
}

/* identify whether this disc is a Gamecube or Wii disc */
static int identify_disc() {
	char readbuf[2048] __attribute__((aligned(32)));

	memset(&internalName[0],0,512);
	// Read the header
	DVD_LowRead64(readbuf, 2048, 0ULL);
	if (readbuf[0]) {
		strncpy(&gameName[0], readbuf, 6);
		gameName[6] = 0;
		// Multi Disc identifier support
		if (readbuf[6]) {
			sprintf(&gameName[0], "%s-disc%i", &gameName[0],
					(readbuf[6]) + 1);
		}
		strncpy(&internalName[0],&readbuf[32],512);
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
				== IS_NGC_DISC) ? B_SELECTED : B_NOSELECT, -1);
		DrawSelectableButton(380, 310, -1, 340, "Wii",
				(type == IS_WII_DISC) ? B_SELECTED : B_NOSELECT, -1);
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
int device_type() {
	int type = TYPE_USB;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Please select the device type");
		DrawSelectableButton(100, 310, -1, 340, "USB",
				(type == TYPE_USB) ? B_SELECTED : B_NOSELECT, -1);
		DrawSelectableButton(380, 310, -1, 340, "Front SD",
				(type == TYPE_SD) ? B_SELECTED : B_NOSELECT, -1);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			type ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	return type;
}

/* the user must specify the file system type */
int filesystem_type() {
	int type = TYPE_FAT;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(255, "Please select the filesystem type");
		DrawSelectableButton(100, 310, -1, 340, "FAT",
				(type == TYPE_FAT) ? B_SELECTED : B_NOSELECT, -1);
		DrawSelectableButton(380, 310, -1, 340, "NTFS",
				(type == TYPE_NTFS) ? B_SELECTED : B_NOSELECT, -1);
		DrawFrameFinish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 btns = get_buttons_pressed();
		if (btns & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (btns & PAD_BUTTON_LEFT)
			type ^= 1;
		if (btns & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));
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
			((disc_type == IS_WII_DISC) ? MAX_WII_OPTIONS : MAX_NGC_OPTIONS) -1;

	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		DrawFrameStart();
		DrawEmptyBox(75, 120, vmode->fbWidth - 78, 400, COLOR_BLACK);
		sprintf(txtbuffer, "%s Disc Ripper Setup:",
				disc_type == IS_WII_DISC ? "Wii" : "Gamecube");
		WriteCentre(130, txtbuffer);

		// Gamecube Settings
		if (disc_type == IS_NGC_DISC) {
		/*
			WriteFont(80, 160 + (32* 1 ), "Shrink ISO");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, getShrinkOption(), (!currentSettingPos) ? B_SELECTED:B_NOSELECT);
			WriteFont(80, 160+(32*2), "Align Files");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, getAlignOption(), (currentSettingPos==1) ? B_SELECTED:B_NOSELECT);
			WriteFont(80, 160+(32*3), "Alignment boundary");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, getAlignmentBoundaryOption(), (currentSettingPos==2) ? B_SELECTED:B_NOSELECT);
		*/
		}
		// Wii Settings
		else if(disc_type == IS_WII_DISC) {
			WriteFont(80, 160+(32*1), "Dual Layer");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, getDualLayerOption(), (!currentSettingPos) ? B_SELECTED:B_NOSELECT, -1);
			WriteFont(80, 160+(32*2), "Chunk Size");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, getChunkSizeOption(), (currentSettingPos==1) ? B_SELECTED:B_NOSELECT, -1);
			WriteFont(80, 160+(32*3), "New device per chunk");
			DrawSelectableButton(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, getNewFileOption(), (currentSettingPos==2) ? B_SELECTED:B_NOSELECT, -1);
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

void prompt_new_file(FILE **fp, int chunk, int type, int fs, int silent) {
	// Close the file and unmount the fs
	fclose(*fp);
	if(silent == ASK_USER) {
		if (fs == TYPE_FAT) {
			fatUnmount("fat:");
			if (type == TYPE_SD) {
				sdcard->shutdown();
			} 
#ifdef HW_RVL
			else if (type == TYPE_USB) {
				usb->shutdown();
			}
#endif
		}
#ifdef HW_RVL
		if (fs == TYPE_NTFS) {
			ntfsUnmount(&rawNTFSMount[0], true);
			free(mounts);
			usb->shutdown();
		}
#endif
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
					ret = fatMountSimple("fat", type == TYPE_USB ? usb : sdcard);
					if (ret == 1) {
						break;
					}
				}
			} 
#ifdef HW_RVL
			else if (fs == TYPE_NTFS) {
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
#endif
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

	*fp = NULL;
	sprintf(txtbuffer, "%s%s.part%i.iso", &mountPath[0], &gameName[0], chunk);
	remove(&txtbuffer[0]);
	*fp = fopen(&txtbuffer[0], "wb");
	if (*fp == NULL) {
		DrawFrameStart();
		DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
		WriteCentre(230, "Failed to create file:");
		WriteCentre(255, txtbuffer);
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
	remove(&txtbuffer[0]);
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		char bca_data[64] __attribute__((aligned(32)));
		DCZeroRange(bca_data, 64);
		DCFlushRange(bca_data, 64);
		dvd_read_bca(bca_data);
		fwrite(bca_data, 1, 0x40, fp);
		fclose(fp);
	}
}

void dump_info(char *md5, char *sha1, u32 crc32, int verified, u32 seconds) {
	char infoLine[1024];
	memset(infoLine, 0, 1024);
	if(md5 && sha1 && crc32) {
		sprintf(infoLine, "--File Generated by CleanRip v%i.%i.%i--"
						  "\r\n\r\nFilename: %s\r\nInternal Name: %s\r\nMD5: %s\r\n"
						  "SHA-1: %s\r\nCRC32: %08lX\r\nVersion: 1.0%i\r\nVerified: %s\r\nSeconds: %lu\r\n",
				V_MAJOR,V_MID,V_MINOR,&gameName[0],&internalName[0], md5, sha1, crc32, *(u8*)0x80000007,
				verified ? "Yes" : "No", seconds);
	}
	else {
		sprintf(infoLine, "--File Generated by CleanRip v%i.%i.%i--"
						  "\r\n\r\nFilename: %s\r\nInternal Name: %s\r\n"
						  "Version: 1.0%i\r\nChecksum calculations disabled\r\nSeconds: %lu\r\n",
				V_MAJOR,V_MID,V_MINOR,&gameName[0],&internalName[0], *(u8*)0x80000007, seconds);
	}
	sprintf(txtbuffer, "%s%s-dumpinfo.txt", &mountPath[0], &gameName[0]);
	remove(&txtbuffer[0]);
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		fwrite(infoLine, 1, strlen(&infoLine[0]), fp);
		fclose(fp);
	}
}

#define MSG_COUNT 8
#define THREAD_PRIO 128

void dump_game(int disc_type, int type, int fs) {

	md5_state_t state;
	md5_byte_t digest[16];
	SHA1Context sha;
	u32 crc32 = 0;
	char *buffer;
	mqbox_t msgq, blockq;
	lwp_t writer;
	writer_msg *wmsg;
	writer_msg msg;
	int i;

	MQ_Init(&blockq, MSG_COUNT);
	MQ_Init(&msgq, MSG_COUNT);

	// since libogc is too shitty to be able to get the current thread priority, just force it to a known value
	LWP_SetThreadPriority(0, THREAD_PRIO);
	// writer thread should have same priority so it can be yielded to
	LWP_CreateThread(&writer, writer_thread, (void*)msgq, NULL, 0, THREAD_PRIO);

	// Check if we will ask the user to insert a new device per chunk
	int silent = options_map[WII_NEWFILE];

	// The read size
	int opt_read_size = READ_SIZE;

	u32 startLBA = 0;
	u32 endLBA = disc_type == IS_NGC_DISC ? NGC_DISC_SIZE
			: (options_map[WII_DUAL_LAYER] == DUAL_LAYER ? WII_D9_SIZE
					: WII_D5_SIZE);

	// Work out the chunk size
	u32 chunk_size_wii = options_map[WII_CHUNK_SIZE];
	u32 opt_chunk_size;
	if (chunk_size_wii == CHUNK_MAX) {
		// use 4GB chunks max for FAT drives
		if (fs == TYPE_FAT) {
			opt_chunk_size = 4 * ONE_GIGABYTE - (opt_read_size>>11) - 1;
		} else {
			opt_chunk_size = endLBA + (opt_read_size>>11);
		}
	} else {
		opt_chunk_size = (chunk_size_wii + 1) * ONE_GIGABYTE;
	}

	if (disc_type == IS_NGC_DISC) {
		opt_chunk_size = NGC_DISC_SIZE;
	}

	// Dump the BCA for Nintendo discs
#ifdef HW_RVL
	if (disc_type == IS_WII_DISC || disc_type == IS_NGC_DISC) {
		dump_bca();
	}
#endif

	// Create the read buffers
	buffer = memalign(32, MSG_COUNT*(opt_read_size+sizeof(writer_msg)));
	for (i=0; i < MSG_COUNT; i++) {
		MQ_Send(blockq, (mqmsg_t)(buffer+i*(opt_read_size+sizeof(writer_msg))), MQ_MSG_BLOCK);
	}

	if(calcChecksums) {
		// Reset MD5/SHA-1/CRC
		md5_init(&state);
		SHA1Reset(&sha);
		crc32 = 0;
	}

	// There will be chunks, name accordingly
	if (opt_chunk_size < endLBA) {
		sprintf(txtbuffer, "%s%s.part0.iso", &mountPath[0], &gameName[0]);
	} else {
		sprintf(txtbuffer, "%s%s.iso", &mountPath[0], &gameName[0]);
	}
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
	msg.command = MSG_SETFILE;
	msg.data = fp;
	MQ_Send(msgq, (mqmsg_t)&msg, MQ_MSG_BLOCK);

	int ret = 0;
	u64 copyTime = gettime();
	u64 startTime = gettime();
	int chunk = 1;

	while (!ret && (startLBA + (opt_read_size>>11)) < endLBA) {
		MQ_Receive(blockq, (mqmsg_t*)&wmsg, MQ_MSG_BLOCK);
		if (wmsg==NULL) { // asynchronous write error
			LWP_JoinThread(writer, NULL);
			fclose(fp);
			DrawFrameStart();
			DrawEmptyBox(30, 180, vmode->fbWidth - 38, 350, COLOR_BLACK);
			WriteCentre(255, "Write Error!");
			WriteCentre(315, "Exiting in 10 seconds");
			DrawFrameFinish();
			sleep(10);
			exit(1);
		}

		if (startLBA > (opt_chunk_size * chunk)) {
			u64 wait_begin;
			// wait for writing to finish
			vu32 sema = 0;
			msg.command = MSG_FLUSH;
			msg.data = (void*)&sema;
			MQ_Send(msgq, (mqmsg_t)&msg, MQ_MSG_BLOCK);
			while (!sema)
				LWP_YieldThread();

			// open new file
			wait_begin = gettime();
			prompt_new_file(&fp, chunk, type, fs, silent);
			copyTime = gettime();
			// pretend the wait didn't happen
			startTime += copyTime - wait_begin;

			// set writing file
			msg.command = MSG_SETFILE;
			msg.data = fp;
			MQ_Send(msgq, (mqmsg_t)&msg, MQ_MSG_BLOCK);
			chunk++;
		}

		wmsg->command =  MSG_WRITE;
		wmsg->data = wmsg+1;
		wmsg->length = opt_read_size;
		wmsg->ret_box = blockq;

		// Read from Disc
		ret = DVD_LowRead64(wmsg->data, (u32)opt_read_size, (u64)startLBA << 11);
		MQ_Send(msgq, (mqmsg_t)wmsg, MQ_MSG_BLOCK);
		if(calcChecksums) {
			// Calculate MD5
			md5_append(&state, (const md5_byte_t *) (wmsg+1), (u32) opt_read_size);
			// Calculate SHA-1
			SHA1Input(&sha, (const unsigned char *) (wmsg+1), (u32) opt_read_size);
			// Calculate CRC32
			crc32 = Crc32_ComputeBuf( crc32, wmsg+1, (u32) opt_read_size);
		}

		check_exit_status();

		if (get_buttons_pressed() & PAD_BUTTON_B) {
			ret = -61;
		}
		// Update status every second

		u64 curTime = gettime();
		int timePassed = diff_msec(copyTime, curTime);
		if (timePassed >= 1000) {
			u64 totalTime = diff_msec(startTime, curTime);
			u32 bytes_per_msec = (((u64)startLBA<<11) + opt_read_size) / totalTime;
			u64 remainder = (((u64)endLBA - startLBA)<<11) - opt_read_size;

			u32 etaTime;
			if(disc_type == IS_NGC_DISC) {
				// multiply ETA by 3/4 to account for CAV speed increase
				etaTime = (remainder / bytes_per_msec * 58) / 75000;
			}
			else {
				etaTime = (remainder / bytes_per_msec) / 1000;
			}
			sprintf(txtbuffer, "%dMB %4.0fKB/s - ETA %02d:%02d:%02d",
					(int) (((u64) ((u64) startLBA << 11)) / (1024* 1024 )),
				(float)bytes_per_msec*1000/1024,
				(int)((etaTime/60/60)%60),(int)((etaTime/60)%60),(int)(etaTime%60));
			DrawFrameStart();
			DrawProgressBar((int)((float)((float)startLBA/(float)endLBA)*100), txtbuffer);
      		DrawFrameFinish();
  			copyTime = curTime;
		}
		startLBA+=opt_read_size>>11;
	}
	// Remainder of data
	if(!ret && startLBA < endLBA) {
		MQ_Receive(blockq, (mqmsg_t*)&wmsg, MQ_MSG_BLOCK);
		if (wmsg==NULL) { // asynchronous write error
			LWP_JoinThread(writer, NULL);
			fclose(fp);
			DrawFrameStart();
			DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
			WriteCentre(255,"Write Error!");
			WriteCentre(315,"Exiting in 10 seconds");
			DrawFrameFinish();
			sleep(10);
			exit(1);
		}
		wmsg->command =  MSG_WRITE;
		wmsg->data = wmsg+1;
		wmsg->length = (u32)((endLBA-startLBA)<<11);
		wmsg->ret_box = blockq;

		ret = DVD_LowRead64(wmsg->data, wmsg->length, (u64)startLBA << 11);
		MQ_Send(msgq, (mqmsg_t)wmsg, MQ_MSG_BLOCK);
		if(calcChecksums) {
			// Calculate MD5
			md5_append(&state, (const md5_byte_t*)(wmsg+1), wmsg->length);
			// Calculate SHA-1
			SHA1Input(&sha, (const unsigned char*)(wmsg+1), wmsg->length);
			// Calculate CRC32
			crc32 = Crc32_ComputeBuf( crc32, wmsg+1, wmsg->length);
		}
	}
	if(calcChecksums) {
		md5_finish(&state, digest);
	}

	// signal writer to finish
	MQ_Send(msgq, (mqmsg_t)NULL, MQ_MSG_BLOCK);
	LWP_JoinThread(writer, NULL);
	fclose(fp);

	free(buffer);
	MQ_Close(blockq);
	MQ_Close(msgq);
	
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
		sprintf(txtbuffer,"Copy completed in %lu mins. Press A",diff_sec(startTime, gettime())/60);
		DrawFrameStart();
		DrawEmptyBox (30,180, vmode->fbWidth-38, 350, COLOR_BLACK);
		WriteCentre(190,txtbuffer);
		if(calcChecksums) {
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
			int verified = (verify_is_available(disc_type) && verify_findMD5Sum(&md5sum[0], disc_type));
			sprintf(txtbuffer, "MD5: %s", verified ? "Verified OK" : "");
			WriteCentre(230,txtbuffer);
			WriteCentre(255,verified ? verify_get_name() : "Not Verified with redump.org");
			WriteCentre(280,&md5sum[0]);
			dump_info(&md5sum[0], &sha1sum[0], crc32, verified, diff_sec(startTime, gettime()));
		}
		else {
			dump_info(NULL, NULL, 0, 0, diff_sec(startTime, gettime()));
		}
		WriteCentre(315,"Press  A to continue  B to Exit");
		dvd_motor_off();
		wait_press_A_exit_B();
	}
}

int main(int argc, char **argv) {

	Initialise();
#ifdef HW_RVL
	iosversion = IOS_GetVersion();
#endif
	if(usb_isgeckoalive(1)) {
		usb_flush(1);
		print_usb = 1;
	}
	print_gecko("CleanRip Version %i.%i.%i\r\n",V_MAJOR, V_MID, V_MINOR);
	print_gecko("Arena Size: %iKb\r\n",(SYS_GetArena1Hi()-SYS_GetArena1Lo())/1024);

#ifdef HW_RVL
	print_gecko("Running on IOS ver: %i\r\n", iosversion);
#endif
	show_disclaimer();
#ifdef HW_RVL	
	hardware_checks();
#endif

	// Ask the user if they want checksum calculations enabled this time?
	calcChecksums = DrawYesNoDialog("Enable checksum calculations?", 
									"(Enabling will add about 3 minutes)");

	while (1) {
#ifdef HW_RVL
		int type = device_type();
#else
		int type = TYPE_SD;
#endif
		int fs = TYPE_FAT;

		if (type == TYPE_USB) {
			fs = filesystem_type();
		}

		int ret = -1;
		do {
			ret = initialise_device(type, fs);
		} while (ret != 1);

		if(calcChecksums) {
			// Try to load up redump.org dat files
			verify_init(&mountPath[0]);

			// Ask the user if they want to download new ones
			verify_download(&mountPath[0]);

			// User might've got some new files.
			verify_init(&mountPath[0]);
		}
		
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
