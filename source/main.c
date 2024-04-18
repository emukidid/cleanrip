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
#include "dvd.h"
#include "verify.h"
#include "datel.h"
#include "main.h"
#include "crc32.h"
#include "sha1.h"
#include "md5.h"
#include <fat.h>
#include "m2loader/m2loader.h"

#define DEFAULT_FIFO_SIZE    (256*1024)//(64*1024) minimum

#ifdef HW_RVL
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>
#endif

#include <ntfs.h>
static ntfs_md *mounts = NULL;

#ifdef HW_RVL
const DISC_INTERFACE* sdcard = &__io_wiisd;
const DISC_INTERFACE* usb = &__io_usbstorage;
#endif
#ifdef HW_DOL
#include <sdcard/gcsd.h>
#include <sdcard/card_cmn.h>
#include <sdcard/card_io.h>
static int sdcard_slot = 0;
const DISC_INTERFACE* sdcard = &__io_gcsda;
const DISC_INTERFACE* m2loader = &__io_m2ldr;
const DISC_INTERFACE* usb = NULL;
#endif

static int calculate_checksums = 0;
static int dump_counter = 0;
static char game_name[32];
static char internal_name[512];
static char mount_path[512];
static char wpad_need_scan = 0;
static char pad_need_scan = 0;
int print_usb = 0;
int shutdown = 0;
int which_fb = 0;
u32 ios_version = -1;
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


void check_exit_status(void) {
#ifdef HW_DOL
	if(shutdown == POWER_OFF || shutdown == RETURN_TO_HBC)
		exit(0);
#endif
#ifdef HW_RVL
	if (shutdown == POWER_OFF) {
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	}
	if (shutdown == RETURN_TO_HBC) {
		void (*rld)() = (void(*)()) 0x80001800;
		rld();
	}
#endif
}

#ifdef HW_RVL
u32 get_wii_buttons_pressed(u32 buttons) {
	WPADData *wii_pad;
	if (wpad_need_scan) {
		WPAD_ScanPads();
		wpad_need_scan = 0;
	}
	wii_pad = WPAD_Data(0);

	if (wii_pad->btns_h & WPAD_BUTTON_B) {
		buttons |= PAD_BUTTON_B;
	}

	if (wii_pad->btns_h & WPAD_BUTTON_A) {
		buttons |= PAD_BUTTON_A;
	}

	if (wii_pad->btns_h & WPAD_BUTTON_LEFT) {
		buttons |= PAD_BUTTON_LEFT;
	}

	if (wii_pad->btns_h & WPAD_BUTTON_RIGHT) {
		buttons |= PAD_BUTTON_RIGHT;
	}

	if (wii_pad->btns_h & WPAD_BUTTON_UP) {
		buttons |= PAD_BUTTON_UP;
	}

	if (wii_pad->btns_h & WPAD_BUTTON_DOWN) {
		buttons |= PAD_BUTTON_DOWN;
	}

	if (wii_pad->btns_h & WPAD_BUTTON_HOME) {
		shutdown = RETURN_TO_HBC;
	}
	return buttons;
}
#endif

u32 get_buttons_pressed(void) {
	u32 buttons = 0;

	if (pad_need_scan) {
		PAD_ScanPads();
		pad_need_scan = 0;
	}

#ifdef HW_RVL
	buttons = get_wii_buttons_pressed(buttons);
#endif

	u16 gc_pad = PAD_ButtonsDown(0);

	if (gc_pad & PAD_BUTTON_B) {
		buttons |= PAD_BUTTON_B;
	}

	if (gc_pad & PAD_BUTTON_A) {
		buttons |= PAD_BUTTON_A;
	}

	if (gc_pad & PAD_BUTTON_LEFT) {
		buttons |= PAD_BUTTON_LEFT;
	}

	if (gc_pad & PAD_BUTTON_RIGHT) {
		buttons |= PAD_BUTTON_RIGHT;
	}

	if (gc_pad & PAD_BUTTON_UP) {
		buttons |= PAD_BUTTON_UP;
	}

	if (gc_pad & PAD_BUTTON_DOWN) {
		buttons |= PAD_BUTTON_DOWN;
	}

	if (gc_pad & PAD_TRIGGER_Z) {
		shutdown = RETURN_TO_HBC;
	}
	check_exit_status();
	return buttons;
}

void wait_press_A(void) {
	// Draw the A button
	fbm_draw_A_button(265, 310);
	fbm_frame_finish();
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (!(get_buttons_pressed() & PAD_BUTTON_A));
}

void wait_press_A_exit_B(void) {
	// Draw the A and B buttons
	fbm_draw_A_button(195, 310);
	fbm_draw_B_button(390, 310);
	fbm_frame_finish();
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

static void invalidate_pads() {
	pad_need_scan = wpad_need_scan = 1;
}

/* check for ahbprot */
int have_hw_access(void) {
	if (read32(HW_ARMIRQMASK) && read32(HW_ARMIRQFLAG)) {
		// disable DVD irq for starlet
		mask32(HW_ARMIRQMASK, 1<<18, 0);
		print_gecko("AHBPROT access OK\r\n");
		return 1;
	}
	return 0;
}

void shutdown_wii(void) {
	shutdown = POWER_OFF;
}

/* start up the GameCube/Wii */
static void initialise(void) {
	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	PAD_Init();

#ifdef HW_RVL
	CONF_Init();
	WPAD_Init();
	WPAD_SetIdleTimeout(120);
	WPAD_SetPowerButtonCallback((WPADShutdownCallback) shutdown_wii);
	SYS_SetPowerCallback(shutdown_wii);
#endif

	vmode = VIDEO_GetPreferredMode(NULL);
	VIDEO_Configure(vmode);
	xfb[0] = (u32 *) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	xfb[1] = (u32 *) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(vmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetPostRetraceCallback(invalidate_pads);
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

	font_initialise();
	fbm_initialise();
	which_fb = 0;
}

#ifdef HW_RVL
/* find_ios - borrwed from Tantric */
static int find_ios(u32 ios) {
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
static void hardware_checks(void) {
	if (!have_hw_access()) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(190, "AHBPROT check failed");
		font_write_center(255, "Please install the latest HBC");
		font_write_center(280, "Check the FAQ for more info");
		font_write_center(315, "Press A to exit");
		wait_press_A();
		exit(0);
	}

	int has_ios_58 = find_ios(58);
	print_gecko("IOS 58 Exists: %s\r\n", has_ios_58 ? "YES":"NO");
	if (has_ios_58 && ios_version != 58) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(190, "IOS Version check failed");
		font_write_center(255, "IOS 58 exists but is not in use");
		font_write_center(280, "Dumping to USB will be SLOW!");
		font_write_center(315, "Press  A to continue  B to exit");
		wait_press_A_exit_B();
	}
	if (!has_ios_58) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(190, "IOS Version check failed");
		font_write_center(255, "Please install IOS58");
		font_write_center(280, "Dumping to USB will be SLOW!");
		font_write_center(315, "Press  A to continue  B to exit");
		wait_press_A_exit_B();
	}
}
#endif

static void show_disclaimer(void) {
	fbm_frame_start();
	fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
	font_write_center(190, "Disclaimer");
	font_write_center(230, "The author is not responsible for any");
	font_write_center(255, "damages that could occur to any");
	font_write_center(280, "removable device used within this program");
	fbm_frame_finish();

	fbm_frame_start();
	fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
	font_write_center(190, "Disclaimer");
	font_write_center(230, "The author is not responsible for any");
	font_write_center(255, "damages that could occur to any");
	font_write_center(280, "removable device used within this program");
	font_write_center(315, "Press  A to continue  B to exit");
	sleep(5);
	wait_press_A_exit_B();
}

static int initialise_dvd(void) {
	fbm_frame_start();
	fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
#ifdef HW_DOL
	font_write_center(255, "Insert a GameCube DVD Disc");
#else
	font_write_center(255, "Insert a GC/Wii DVD Disc");
#endif
	font_write_center(315, "Press  A to continue  B to exit");
	wait_press_A_exit_B();

	fbm_frame_start();
	fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
	font_write_center(255, "Initialising Disc ...");
	fbm_frame_finish();
	int ret = dvd_initialise_drive();

	if (ret == NO_DISC) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(255, "No disc detected");
		fbm_frame_finish();
		sleep(3);
	}
	return ret;
}

#ifdef HW_DOL
int select_sd_gecko_slot() {
	int slot = 0;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(255, "Please select SDGecko Slot");
		fbm_draw_selection_button(100, 310, -1, 340, "Slot A", slot == 0 ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button(240, 310, -1, 340, "Slot B", slot == 1 ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button(380, 310, -1, 340, "SD2SP2", slot == 2 ? B_SELECTED : B_NOSELECT, -1);
		fbm_frame_finish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 buttons = get_buttons_pressed();
		if (buttons & PAD_BUTTON_RIGHT) {
			slot++;
			if (slot > 2) slot = 0;
		}
		if (buttons & PAD_BUTTON_LEFT) {
			slot--;
			if (slot < 0) slot = 2;
		}
		if (buttons & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));

	return slot;
}

const DISC_INTERFACE* get_sd_card_handler(int slot) {
	switch (slot) {
		case 1:
			return &__io_gcsdb;
		case 2:
			return &__io_gcsd2;
		default: /* Also handles case 0 */
			return &__io_gcsda;
	}
}
#endif

static int initialise_storage_device(int type, int fs) {
	int ret = 0;

	fbm_frame_start();
	fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
	if (type == TYPE_SD) {
#ifdef HW_DOL
		sdcard_slot = select_sd_gecko_slot();
		sdcard = get_sd_card_handler(sdcard_slot);

		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
#endif
		font_write_center(255, "Insert a SD FAT32/NTFS formatted device");
	}
#ifdef HW_DOL
	else if (type == TYPE_M2LOADER) {
		font_write_center(255, "Insert a M.2 FAT32/NTFS formatted device");
	}
#else
	else if (type == TYPE_USB) {
		font_write_center(255, "Insert a USB FAT32/NTFS formatted device");
	}
#endif
	font_write_center(315, "Press  A to continue  B to exit");
	wait_press_A_exit_B();

	if (fs == TYPE_FAT) {
		switch (type) {
			case TYPE_SD:
				ret = fatMountSimple("fat", sdcard);
				break;
#ifdef HW_DOL
			case TYPE_M2LOADER:
				ret = fatMountSimple("fat", m2loader);
				break;
#else
			case TYPE_USB:
				ret = fatMountSimple("fat", usb);
				break;
#endif
		}
		if (ret != 1) {
			fbm_frame_start();
			fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
			sprintf(txtbuffer, "Error Mounting Device [%08X]", ret);
			font_write_center(255, txtbuffer);
			font_write_center(315, "Press A to try again  B to exit");
			wait_press_A_exit_B();
		}
		sprintf(&mount_path[0], "fat:/");
	}
	else if (fs == TYPE_NTFS) {
		int num_mount = 0;
		switch (type) {
			case TYPE_SD:
				num_mount = ntfsMountDevice(sdcard, &mounts, NTFS_DEFAULT | NTFS_RECOVER);
				break;
#ifdef HW_DOL
			case TYPE_M2LOADER:
				num_mount = ntfsMountDevice(m2loader, &mounts, NTFS_DEFAULT | NTFS_RECOVER);
				break;
#else
			case TYPE_USB:
				num_mount = ntfsMountDevice(usb, &mounts, NTFS_DEFAULT | NTFS_RECOVER);
				break;
#endif
		}

		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		if (!num_mount || num_mount == -1) {
			if (num_mount == -1) {
				sprintf(txtbuffer, "Error whilst mounting devices (%i)", errno);
			} else {
				sprintf(txtbuffer, "No NTFS volume(s) were found and/or mounted");
			}
			font_write_center(255, txtbuffer);
			font_write_center(315, "Press A to try again  B to exit");
			wait_press_A_exit_B();
		} else {
			sprintf(txtbuffer, "%s Mounted", ntfsGetVolumeName(mounts[0].name));
			font_write_center(230, txtbuffer);
			sprintf(txtbuffer, "%i NTFS volume(s) mounted!", num_mount);
			font_write_center(255, txtbuffer);
			font_write_center(315, "Press  A  to continue");
			wait_press_A();
			sprintf(&mount_path[0], "%s:/", mounts[0].name);
			ret = 1;
		}
	}
#ifdef HW_DOL
	if (type == TYPE_SD) {
		sdgecko_setSpeed(sdcard_slot, EXI_SPEED32MHZ);
	}
#endif
	return ret;
}

/* identify whether this disc is a Gamecube or Wii disc */
static int identify_disc(void) {
	char read_buf[2048] __attribute__((aligned(32)));

	memset(&internal_name[0],0,512);
	// Read the header
	dvd_low_read_64(read_buf, 2048, 0ULL);
	if (read_buf[0]) {
		strncpy(&game_name[0], read_buf, 6);
		game_name[6] = 0;
		// Multi Disc identifier support
		if (read_buf[6]) {
			size_t lastPos = strlen(game_name);
			sprintf(&game_name[lastPos], "-disc%i", (read_buf[6]) + 1);
		}
		strncpy(&internal_name[0],&read_buf[32],512);
		internal_name[511] = '\0';
	} else {
		sprintf(&game_name[0], "disc%i", dump_counter);
	}
	if ((*(volatile u32*) (read_buf+0x1C)) == NGC_MAGIC) {
		return IS_NGC_DISC;
	}
	if ((*(volatile u32*) (read_buf+0x18)) == WII_MAGIC) {
		return IS_WII_DISC;
	} else {
		return IS_UNK_DISC;
	}
}

const char* const get_game_name(void) {
	return game_name;
}

/* the user must specify the disc type */
static int force_disc(void) {
	int type = IS_NGC_DISC;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(190, "Failed to detect the disc type");
		font_write_center(255, "Please select the correct type");
		fbm_draw_selection_button(100, 310, -1, 340, "Gamecube", (type
				== IS_NGC_DISC) ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button(380, 310, -1, 340, "Wii",
				(type == IS_WII_DISC) ? B_SELECTED : B_NOSELECT, -1);
		fbm_frame_finish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
		u32 buttons = get_buttons_pressed();
		if (buttons & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (buttons & PAD_BUTTON_LEFT)
			type ^= 1;
		if (buttons & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)))
			;
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A))
		;
	return type;
}

/*
 Detect if a dual-layer disc was inserted by checking if reading from sectors
 on the second layer is successful or not. Returns the correct disc size.
*/
int detect_duallayer_disc(void) {
	char read_buf[64] __attribute__((aligned(32)));
	uint64_t offset_to_second_layer = (uint64_t)WII_D5_SIZE << 11;
	int disc_size = WII_D5_SIZE;
	if (dvd_low_read_64(read_buf, 64, offset_to_second_layer) == 0) {
		disc_size = WII_D9_SIZE;
	}
	return disc_size;
}

/* the user must specify the device type */
int select_storage_device_type(void) {
	int selected_type = 0;
	while (1) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(255, "Please select the device type");
#ifdef HW_DOL
		fbm_draw_selection_button(140, 310, -1, 340, "SD Card",
				(selected_type == 0) ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button(380, 310, -1, 340, "M.2 Loader",
				(selected_type == 1) ? B_SELECTED : B_NOSELECT, -1);
#endif
#ifdef HW_RVL
		fbm_draw_selection_button(100, 310, -1, 340, "USB",
				(selected_type == 0) ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button(380, 310, -1, 340, "Front SD",
				(selected_type == 1) ? B_SELECTED : B_NOSELECT, -1);
#endif
		fbm_frame_finish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 buttons = get_buttons_pressed();

		if (buttons & PAD_BUTTON_RIGHT)
			selected_type ^= 1;
		if (buttons & PAD_BUTTON_LEFT)
			selected_type ^= 1;

		if (buttons & PAD_BUTTON_A)
			break;

		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));

#ifdef HW_DOL
	return selected_type == 0 ? TYPE_SD : TYPE_M2LOADER;
#endif
#ifdef HW_RVL
	return selected_type == 0 ? TYPE_USB : TYPE_SD;
#endif
}

/* the user must specify the file system type */
int select_filesystem_type() {
	int type = TYPE_FAT;
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(255, "Please select the filesystem type");
		fbm_draw_selection_button(100, 310, -1, 340, "FAT",
				(type == TYPE_FAT) ? B_SELECTED : B_NOSELECT, -1);
		fbm_draw_selection_button(380, 310, -1, 340, "NTFS",
				(type == TYPE_NTFS) ? B_SELECTED : B_NOSELECT, -1);
		fbm_frame_finish();
		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
		u32 buttons = get_buttons_pressed();
		if (buttons & PAD_BUTTON_RIGHT)
			type ^= 1;
		if (buttons & PAD_BUTTON_LEFT)
			type ^= 1;
		if (buttons & PAD_BUTTON_A)
			break;
		while ((get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT
				| PAD_BUTTON_B | PAD_BUTTON_A)));
	}
	while ((get_buttons_pressed() & PAD_BUTTON_A));
	return type;
}

char *get_shrink_option(void) {
	int opt = options_map[NGC_SHRINK_ISO];
	if (opt == SHRINK_ALL)
		return "Shrink All";
	else if (opt == SHRINK_PAD_GARBAGE)
		return "Wipe Garbage";
	else if (opt == SHRINK_NONE)
		return "No";
	return 0;
}

char *get_align_option(void) {
	int opt = options_map[NGC_ALIGN_FILES];
	if (opt == ALIGN_ALL)
		return "Align All";
	else if (opt == ALIGN_AUDIO)
		return "Audio Only";
	return 0;
}

char *get_alignment_boundary_option(void) {
	int opt = options_map[NGC_ALIGN_BOUNDARY];
	if (opt == ALIGN_32)
		return "32Kb";
	else if (opt == ALIGN_2)
		return "2KB";
	else if (opt == ALIGN_512)
		return "512B";
	return 0;
}

char *get_dual_layer_option(void) {
	int opt = options_map[WII_DUAL_LAYER];
	if (opt == AUTO_DETECT)
		return "Auto";
	else if (opt == SINGLE_LAYER)
		return "No";
	else if (opt == DUAL_LAYER)
		return "Yes";
	return 0;
}

char *get_new_file_option(void) {
	int opt = options_map[WII_NEWFILE];
	if (opt == ASK_USER)
		return "Yes";
	else if (opt == AUTO_CHUNK)
		return "No";
	return 0;
}

char *get_chunk_size_option(void) {
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

int get_max_option_pos(int option_pos) {
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

void toggle_option(int option_pos, int direction) {
	int max = get_max_option_pos(option_pos);
	if (options_map[option_pos] + direction >= max) {
		options_map[option_pos] = 0;
	} else if (options_map[option_pos] + direction < 0) {
		options_map[option_pos] = max - 1;
	} else {
		options_map[option_pos] += direction;
	}
}

static void get_settings(int disc_type) {
	int current_setting_pos = 0;
	int max_setting_pos = (disc_type == IS_WII_DISC ? MAX_WII_OPTIONS : MAX_NGC_OPTIONS) - 1;

	while ((get_buttons_pressed() & PAD_BUTTON_A));
	while (1) {
		fbm_frame_start();
		fbm_draw_box(75, 120, vmode->fbWidth - 78, 400);
		sprintf(txtbuffer, "%s Disc Ripper Setup:",
				disc_type == IS_WII_DISC ? "Wii" : "Gamecube");
		font_write_center(130, txtbuffer);

		// Gamecube Settings
		if (disc_type == IS_NGC_DISC) {
		/*
			font_write(80, 160 + (32* 1 ), "Shrink ISO");
			fbm_draw_selection_button(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, get_shrink_option(), (!current_setting_pos) ? B_SELECTED:B_NOSELECT);
			font_write(80, 160+(32*2), "Align Files");
			fbm_draw_selection_button(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, get_align_option(), (current_setting_pos==1) ? B_SELECTED:B_NOSELECT);
			font_write(80, 160+(32*3), "Alignment boundary");
			fbm_draw_selection_button(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, getAlignmentBoundaryOption(), (current_setting_pos==2) ? B_SELECTED:B_NOSELECT);
		*/
		}
		// Wii Settings
		else if(disc_type == IS_WII_DISC) {
			font_write(80, 160+(32*1), "Dual Layer");
			fbm_draw_selection_button(vmode->fbWidth-220, 160+(32*1), -1, 160+(32*1)+30, get_dual_layer_option(), (!current_setting_pos) ? B_SELECTED:B_NOSELECT, -1);
			font_write(80, 160+(32*2), "Chunk Size");
			fbm_draw_selection_button(vmode->fbWidth-220, 160+(32*2), -1, 160+(32*2)+30, get_chunk_size_option(), (current_setting_pos==1) ? B_SELECTED:B_NOSELECT, -1);
			font_write(80, 160+(32*3), "New device per chunk");
			fbm_draw_selection_button(vmode->fbWidth-220, 160+(32*3), -1, 160+(32*3)+30, get_new_file_option(), (current_setting_pos==2) ? B_SELECTED:B_NOSELECT, -1);
		}
		font_write_center(370,"Press  A  to continue");
		fbm_draw_A_button(265,360);
		fbm_frame_finish();

		while (!(get_buttons_pressed() & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT | PAD_BUTTON_A | PAD_BUTTON_UP | PAD_BUTTON_DOWN)));
		u32 buttons = get_buttons_pressed();
		if(buttons & PAD_BUTTON_RIGHT) {
			toggle_option(current_setting_pos+((disc_type == IS_WII_DISC)?MAX_NGC_OPTIONS:0), 1);
		}
		if(buttons & PAD_BUTTON_LEFT) {
			toggle_option(current_setting_pos+((disc_type == IS_WII_DISC)?MAX_NGC_OPTIONS:0), -1);
		}
		if(buttons & PAD_BUTTON_UP) {
			current_setting_pos = (current_setting_pos>0) ? (current_setting_pos-1):max_setting_pos;
		}
		if(buttons & PAD_BUTTON_DOWN) {
			current_setting_pos = (current_setting_pos<max_setting_pos) ? (current_setting_pos+1):0;
		}
		if(buttons & PAD_BUTTON_A) {
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
#ifdef HW_DOL
			else if (type == TYPE_M2LOADER) {
				m2loader->shutdown();
			}
#else
			else if (type == TYPE_USB) {
				usb->shutdown();
			}
#endif
		}
		else if (fs == TYPE_NTFS) {
			ntfsUnmount(mounts[0].name, true);
			free(mounts);
			if (type == TYPE_SD) {
				sdcard->shutdown();
			}
#ifdef HW_DOL
			else if (type == TYPE_M2LOADER) {
				m2loader->shutdown();
			}
#else
			else if (type == TYPE_USB) {
				usb->shutdown();
			}
#endif
		}
		// Stop the disc if we're going to wait on the user
		dvd_motor_off();
	}

	if(silent == ASK_USER) {
		int ret = -1;
		do {
				fbm_frame_start();
				fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
				font_write_center(255, "Insert a device for the next chunk");
				font_write_center(315, "Press  A to continue  B to exit");
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
			else if (fs == TYPE_NTFS) {
				int num_mount = ntfsMountDevice(type == TYPE_USB ? usb : sdcard,
						&mounts, NTFS_DEFAULT | NTFS_RECOVER);
				if (num_mount && num_mount != -1) {
					sprintf(&mount_path[0], "%s:/", mounts[0].name);
					ret = 1;
				} else {
					ret = -1;
				}
			}
			if (ret != 1) {
				fbm_frame_start();
				fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
				sprintf(txtbuffer, "Error Mounting Device [%08X]", ret);
				font_write_center(255, txtbuffer);
				font_write_center(315, "Press A to try again  B to exit");
				wait_press_A_exit_B();
			}
		} while (ret != 1);
	}

	*fp = NULL;
	sprintf(txtbuffer, "%s%s.part%i.iso", &mount_path[0], &game_name[0], chunk);
	remove(&txtbuffer[0]);
	*fp = fopen(&txtbuffer[0], "wb");
	if (*fp == NULL) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(230, "Failed to create file:");
		font_write_center(255, txtbuffer);
		font_write_center(315, "Exiting in 5 seconds");
		fbm_frame_finish();
		sleep(5);
		exit(0);
	}
	if(silent == ASK_USER) {
		dvd_initialise_drive();
	}
}

void write_dump_bca() {
	sprintf(txtbuffer, "%s%s.bca", &mount_path[0], &game_name[0]);
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

void write_dump_info(char *md5, char *sha1, u32 crc32, int verified, u32 seconds) {
	char dump_info[1024];
	memset(dump_info, 0, 1024);
	if(md5 && sha1 && crc32) {
		sprintf(dump_info, "--File Generated by CleanRip v%i.%i.%i--"
						  "\r\n\r\nFilename: %s\r\nInternal Name: %s\r\nMD5: %s\r\n"
						  "SHA-1: %s\r\nCRC32: %08X\r\nVersion: 1.0%i\r\nVerified: %s\r\nDuration: %u min. %u sec.\r\n",
				V_MAJOR,V_MID,V_MINOR,&game_name[0],&internal_name[0], md5, sha1, crc32, *(u8*)0x80000007,
				verified ? "Yes" : "No", seconds/60, seconds%60);
	}
	else {
		sprintf(dump_info, "--File Generated by CleanRip v%i.%i.%i--"
						  "\r\n\r\nFilename: %s\r\nInternal Name: %s\r\n"
						  "Version: 1.0%i\r\nChecksum calculations disabled\r\nDuration: %u min. %u sec.\r\n",
				V_MAJOR,V_MID,V_MINOR,&game_name[0],&internal_name[0], *(u8*)0x80000007, seconds/60, seconds%60);
	}
	sprintf(txtbuffer, "%s%s-dumpinfo.txt", &mount_path[0], &game_name[0]);
	remove(&txtbuffer[0]);
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		fwrite(dump_info, 1, strlen(&dump_info[0]), fp);
		fclose(fp);
	}
}

#define MSG_COUNT 8
#define THREAD_PRIO 128

int dump_game(int disc_type, int type, int fs) {

	md5_state_t state;
	md5_byte_t digest[16];
	SHA1Context sha;
	u32 crc32 = 0;
	u32 crc100000 = 0;
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
	u32 opt_read_size = READ_SIZE;

	u32 start_LBA = 0;
	u32 end_LBA = (disc_type == IS_NGC_DISC || disc_type == IS_DATEL_DISC) ? NGC_DISC_SIZE
			: (options_map[WII_DUAL_LAYER] == AUTO_DETECT ? detect_duallayer_disc()
				: (options_map[WII_DUAL_LAYER] == DUAL_LAYER ? WII_D9_SIZE 
					: WII_D5_SIZE));

	// Work out the chunk size
	u32 chunk_size_wii = options_map[WII_CHUNK_SIZE];
	u32 opt_chunk_size;
	if (chunk_size_wii == CHUNK_MAX) {
		// use 4GB chunks max for FAT drives
		if (fs == TYPE_FAT) {
			opt_chunk_size = 4 * ONE_GIGABYTE - (opt_read_size>>11) - 1;
		} else {
			opt_chunk_size = end_LBA + (opt_read_size>>11);
		}
	} else {
		opt_chunk_size = (chunk_size_wii + 1) * ONE_GIGABYTE;
	}

	if (disc_type == IS_NGC_DISC || disc_type == IS_DATEL_DISC) {
		opt_chunk_size = NGC_DISC_SIZE;
	}

	// Dump the BCA for Nintendo discs
#ifdef HW_RVL
	write_dump_bca();
#endif

	// Create the read buffers
	buffer = memalign(32, MSG_COUNT*(opt_read_size+sizeof(writer_msg)));
	for (i=0; i < MSG_COUNT; i++) {
		MQ_Send(blockq, (mqmsg_t)(buffer+i*(opt_read_size+sizeof(writer_msg))), MQ_MSG_BLOCK);
	}

	// Reset MD5/SHA-1/CRC
	md5_init(&state);
	SHA1Reset(&sha);
	crc32 = 0;

	// There will be chunks, name accordingly
	if (opt_chunk_size < end_LBA) {
		sprintf(txtbuffer, "%s%s.part0.iso", &mount_path[0], &game_name[0]);
	} else {
		sprintf(txtbuffer, "%s%s.iso", &mount_path[0], &game_name[0]);
	}
	remove(&txtbuffer[0]);
	FILE *fp = fopen(&txtbuffer[0], "wb");
	if (fp == NULL) {
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		font_write_center(230, "Failed to create file:");
		font_write_center(255, txtbuffer);
		font_write_center(315, "Exiting in 5 seconds");
		fbm_frame_finish();
		sleep(5);
		exit(0);
	}
	msg.command = MSG_SETFILE;
	msg.data = fp;
	MQ_Send(msgq, (mqmsg_t)&msg, MQ_MSG_BLOCK);

	int ret = 0;
	u32 last_LBA = 0;
	u64 last_checked_time = gettime();
	u64 start_time = gettime();
	int chunk = 1;
	int is_known_datel = 0;

	while (!ret && (start_LBA < end_LBA)) {
		MQ_Receive(blockq, (mqmsg_t*)&wmsg, MQ_MSG_BLOCK);
		if (wmsg==NULL) { // asynchronous write error
			LWP_JoinThread(writer, NULL);
			fclose(fp);
			fbm_frame_start();
			fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
			font_write_center(255, "Write Error!");
			font_write_center(315, "Exiting in 10 seconds");
			fbm_frame_finish();
			sleep(10);
			exit(1);
		}

		if (start_LBA > (opt_chunk_size * chunk)) {
			// wait for writing to finish
			vu32 sema = 0;
			msg.command = MSG_FLUSH;
			msg.data = (void*)&sema;
			MQ_Send(msgq, (mqmsg_t)&msg, MQ_MSG_BLOCK);
			while (!sema)
				LWP_YieldThread();

			// open new file
			u64 wait_begin = gettime();
			prompt_new_file(&fp, chunk, type, fs, silent);
			// pretend the wait didn't happen
			start_time -= (gettime() - wait_begin);

			// set writing file
			msg.command = MSG_SETFILE;
			msg.data = fp;
			MQ_Send(msgq, (mqmsg_t)&msg, MQ_MSG_BLOCK);
			chunk++;
		}

		opt_read_size = (start_LBA + (opt_read_size>>11)) <= end_LBA ? opt_read_size : ((u32)((end_LBA-start_LBA)<<11));

		wmsg->command =  MSG_WRITE;
		wmsg->data = wmsg+1;
		wmsg->length = opt_read_size;
		wmsg->ret_box = blockq;

		// Read from Disc
		if(disc_type == IS_DATEL_DISC)
			ret = dvd_low_read_64_datel(wmsg->data, (u32)opt_read_size, (u64)start_LBA << 11, is_known_datel);
		else
			ret = dvd_low_read_64(wmsg->data, (u32)opt_read_size, (u64)start_LBA << 11);
		MQ_Send(msgq, (mqmsg_t)wmsg, MQ_MSG_BLOCK);
		if(calculate_checksums) {
			// Calculate MD5
			md5_append(&state, (const md5_byte_t *) (wmsg+1), (u32) opt_read_size);
			// Calculate SHA-1
			SHA1Input(&sha, (const unsigned char *) (wmsg+1), (u32) opt_read_size);
			// Calculate CRC32
			crc32 = Crc32_ComputeBuf( crc32, wmsg+1, (u32) opt_read_size);
		}

		if(disc_type == IS_DATEL_DISC && (((u64)start_LBA<<11) + opt_read_size == 0x100000)){
			crc100000 = crc32;
			is_known_datel = datel_find_crc_sum(crc100000);
			fbm_frame_start();
			fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
			if(!is_known_datel) {
				font_write_center(215, "(Warning: This disc will take a while to dump!)");
			}
			sprintf(txtbuffer, "%s CRC100000=%08X", (is_known_datel ? "Known":"Unknown"), crc100000);
			font_write_center(255, txtbuffer);
			font_write_center(315, "Press  A to continue  B to exit");
			u64 waitTimeStart = gettime();
			wait_press_A_exit_B();
			start_time += (gettime() - waitTimeStart);	// Don't throw time off because we'd paused here
		}

		check_exit_status();

		if (get_buttons_pressed() & PAD_BUTTON_B) {
			ret = -61;
		}
		// Update status every second
		u64 curTime = gettime();
		s32 timePassed = diff_msec(last_checked_time, curTime);
		if (timePassed >= 1000) {
			u32 bytes_since_last_read = (u32)(((start_LBA - last_LBA)<<11) * (1000.0f/timePassed));
			u64 remainder = (((u64)end_LBA - start_LBA)<<11) - opt_read_size;

			u32 etaTime = (remainder / bytes_since_last_read);
			sprintf(txtbuffer, "%dMB %4.2fKB/s - ETA %02d:%02d:%02d",
					(int) (((u64) ((u64) start_LBA << 11)) / (1024*1024)),
				(float)bytes_since_last_read/1024.0f,
				(int)((etaTime/3600)%60),(int)((etaTime/60)%60),(int)(etaTime%60));
			fbm_frame_start();
			fbm_draw_progress_bar((int)((float)((float)start_LBA/(float)end_LBA)*100), txtbuffer);
      		fbm_frame_finish();
  			last_checked_time = curTime;
			last_LBA = start_LBA;
		}
		start_LBA+=opt_read_size>>11;
	}
	if(calculate_checksums) {
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
		fbm_frame_start();
		fbm_draw_box (30,180, vmode->fbWidth-38, 350);
		sprintf(txtbuffer, "%s",dvd_error_str());
		font_write_center(255,txtbuffer);
		font_write_center(315,"Press  A  to continue");
		dvd_motor_off();
		wait_press_A();
		return 0;
	}
	else if (ret == -61) {
		fbm_frame_start();
		fbm_draw_box (30,180, vmode->fbWidth-38, 350);
		sprintf(txtbuffer, "Copy Cancelled");
		font_write_center(255,txtbuffer);
		font_write_center(315,"Press  A  to continue");
		dvd_motor_off();
		wait_press_A();
		return 0;
	}
	else {
		sprintf(txtbuffer,"Copy completed in %u mins. Press A",diff_sec(start_time, gettime())/60);
		fbm_frame_start();
		fbm_draw_box (30,180, vmode->fbWidth-38, 350);
		font_write_center(190,txtbuffer);
		if(calculate_checksums) {
			char md5sum[64];
			char sha1sum[64];
			memset(&md5sum[0], 0, 64);
			memset(&sha1sum[0], 0, 64);
			int i; for (i=0; i<16; i++) sprintf(&md5sum[i*2],"%02x",digest[i]);
			if(SHA1Result(&sha)) {
				for (i=0; i<5; i++) sprintf(&sha1sum[i*8],"%08x",sha.Message_Digest[i]);
			}
			else {
				sprintf(sha1sum, "Error computing SHA-1");
			}
			int verified = (verify_is_available(disc_type) && verify_find_md5(&md5sum[0], disc_type));
			sprintf(txtbuffer, "MD5: %s", verified ? "Verified OK" : "");
			font_write_center(230,txtbuffer);
			font_write_center(255,verified ? verify_get_name() : "Not Verified with redump.org");
			font_write_center(280,&md5sum[0]);
			write_dump_info(&md5sum[0], &sha1sum[0], crc32, verified, diff_sec(start_time, gettime()));
		}
		else {
			write_dump_info(NULL, NULL, 0, 0, diff_sec(start_time, gettime()));
		}
		if((disc_type == IS_DATEL_DISC)) {
			datel_write_dump_skips(&mount_path[0], crc100000);
		}
		font_write_center(315,"Press  A to continue  B to exit");
		dvd_motor_off();
		wait_press_A_exit_B();
	}
	return 1;
}

int main(int argc, char **argv) {

	initialise();
#ifdef HW_RVL
	ios_version = IOS_GetVersion();
#endif
	if(usb_isgeckoalive(1)) {
		usb_flush(1);
		print_usb = 1;
	}
	print_gecko("CleanRip Version %i.%i.%i\r\n",V_MAJOR, V_MID, V_MINOR);
	print_gecko("Arena Size: %iKb\r\n",(SYS_GetArena1Hi()-SYS_GetArena1Lo())/1024);

#ifdef HW_RVL
	print_gecko("Running on IOS ver: %i\r\n", ios_version);
#endif
	show_disclaimer();
#ifdef HW_RVL
	hardware_checks();
#endif

	// Ask the user if they want checksum calculations enabled this time?
	calculate_checksums = fbm_draw_yes_no_dialog("Enable checksum calculations?",
									"(Enabling will add about 3 minutes)");

	int reuse_settings = NOT_ASKED;
	while (1) {
		int type, fs, ret;
		if(reuse_settings == NOT_ASKED || reuse_settings == ANSWER_NO) {
			type = select_storage_device_type();
			fs = select_filesystem_type();

			ret = -1;
			do {
				ret = initialise_storage_device(type, fs);
			} while (ret != 1);
		}

		if(calculate_checksums) {
			// Try to load up redump.org dat files
			verify_initialise(&mount_path[0]);
#ifdef HW_RVL
			// Ask the user if they want to download new ones
			verify_download_DAT(&mount_path[0]);

			// User might've got some new files.
			verify_initialise(&mount_path[0]);
#endif
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

		if(reuse_settings == NOT_ASKED || reuse_settings == ANSWER_NO) {
			if (disc_type == IS_WII_DISC) {
				get_settings(disc_type);
			}
		
			// Ask the user if they want to force Datel check this time?
			if(fbm_draw_yes_no_dialog("Is this a unlicensed datel disc?",
								 "(Will attempt auto-detect if no)")) {
				disc_type = IS_DATEL_DISC;
				datel_init(&mount_path[0]);
#ifdef HW_RVL
				datel_download(&mount_path[0]);
				datel_init(&mount_path[0]);
#endif
				calculate_checksums = 1;
			}
		}
		
		if(reuse_settings == NOT_ASKED) {
			if(fbm_draw_yes_no_dialog("Remember settings?",
								 "Will only ask again next session")) {
				reuse_settings = ANSWER_YES;
			}
		}

		verify_in_use = verify_is_available(disc_type);
		verify_disc_type = disc_type;

		ret = dump_game(disc_type, type, fs);
		verify_in_use = 0;
		dump_counter += (ret ? 1 : 0);
		
		fbm_frame_start();
		fbm_draw_box(30, 180, vmode->fbWidth - 38, 350);
		sprintf(txtbuffer, "%i disc(s) dumped", dump_counter);
		font_write_center(190, txtbuffer);
		font_write_center(255, "Dump another disc?");
		font_write_center(315, "Press  A to continue  B to exit");
		wait_press_A_exit_B();
	}

	return 0;
}
