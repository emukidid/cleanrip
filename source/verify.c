/**
 * CleanRip - verify.c
 * Copyright (C) 2010 emu_kidid
 *
 * Uses redump.org .dat files to verify MD5 sums using XML
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

#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <ogcsys.h>
#include <string.h>
#include <sys/dir.h>
#include <network.h>
#include <mxml.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "http.h"
#include "main.h"
#include "verify.h"

static int verify_initialized = 0;
static char game_name_DAT[256];

// Pointers to the redump.org DAT files contents.
static char *ngc_DAT = NULL;
#ifdef HW_RVL
static char *wii_DAT = NULL;
int net_initialized = 0;
static int ask_DAT_download = 1;
#endif

// Parsed XML data from the redump.org DAT files.
static mxml_node_t *ngc_XML = NULL;
#ifdef HW_RVL
static mxml_node_t *wii_XML = NULL;
#endif

void verify_initialise(char *mount_path) {
	if (verify_initialized) {
		return;
	}
	
	if(ngc_DAT) {
		if(ngc_XML) {
			mxmlDelete(ngc_XML);
			free(ngc_XML);
		}
		free(ngc_DAT);
	}
#ifdef HW_RVL
	if(wii_DAT) {
		free(wii_DAT);
		if(wii_XML) {
			mxmlDelete(wii_XML);
			free(wii_XML);
		}
	}
#endif

	mxmlSetErrorCallback((mxml_error_cb_t)print_gecko);
	FILE *fp = NULL;
	// Check for the Gamecube Redump.org DAT and read it
	sprintf(txtbuffer, "%sgc.dat", mount_path);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		int file_size_DAT = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (file_size_DAT > 0) {
			ngc_DAT = (char*) memalign(32, file_size_DAT);
			if (ngc_DAT) {
				fread(ngc_DAT, 1, file_size_DAT, fp);
			}		
		}
		fclose(fp);
		fp = NULL;
	}

	if (ngc_DAT) {
		ngc_XML = mxmlLoadString(NULL, ngc_DAT, MXML_OPAQUE_CALLBACK);
	}

#ifdef HW_RVL
	// Check for the Wii Redump.org DAT and read it
	sprintf(txtbuffer, "%swii.dat", mount_path);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		fseek(fp, 0L, SEEK_END);
		int file_size_DAT = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (file_size_DAT > 0) {
			wii_DAT = (char*) memalign(32, file_size_DAT);
			if (wii_DAT) {
				fread(wii_DAT, 1, file_size_DAT, fp);
			}
		}	
		fclose(fp);
		fp = NULL;
	}
	
	if (wii_DAT) {
		wii_XML = mxmlLoadString(NULL, wii_DAT, MXML_OPAQUE_CALLBACK);
	}
#endif // #ifdef HW_RVL

#ifdef HW_RVL
	print_gecko("DAT Files [NGC: %s] [Wii: %s]\r\n", ngc_DAT ? "YES":"NO", wii_DAT ? "YES":"NO");
	verify_initialized = ((ngc_DAT && ngc_XML) && (wii_DAT && wii_XML));
#else
	print_gecko("DAT Files [NGC: %s]\r\n", ngc_DAT ? "YES":"NO");
	verify_initialized = (ngc_DAT && ngc_XML);
#endif
}

#ifdef HW_RVL
// If there was some new files obtained, return 1, else 0
void verify_download_DAT(char *mount_path) {
	if(!ask_DAT_download) {
		return;
	}
	
	int res = 0;
	// Ask the user if they want to update from the web
	if(verify_initialized) {
		char *line1 = "redump.org DAT files found";
		char *line2 = "Check for updated DAT files?";
		res = DrawYesNoDialog(line1, line2);
	}
	else {
		char *line1 = "redump.org DAT files not found";
		char *line2 = "Download them now?";
		res = DrawYesNoDialog(line1, line2);
	}
	
	// If yes, lets download an update
	if(res) {
		// Initialize the network
		if(!net_initialized) {
			char ip[16];
			DrawMessageBox(D_INFO, "Checking for DAT updates\n \nInitializing Network...");
			res = if_config(ip, NULL, NULL, true);
      		if(res >= 0) {
	      		sprintf(txtbuffer, "Checking for DAT updates\nNetwork Initialized!\nIP: %s", ip);
	      		DrawMessageBox(D_INFO, txtbuffer);
				net_initialized = 1;
				print_gecko("Network Initialized!\r\n");
			}
      		else {
	      		DrawMessageBox(D_FAIL, "Checking for DAT updates\nNetwork failed to Initialize!");
	      		sleep(5);
        		net_initialized = 0;
				print_gecko("Network Failed to Initialize!\r\n");
        		return;
      		}
  		}

		u8 *xml_file = (u8*)memalign(32, MAX_VERIFY_DAT_SIZE);
		char file_path_DAT[64];

  		// Download the GC DAT
  		sprintf(file_path_DAT, "%sgc.dat", mount_path);
		if((res = http_request("www.gc-forever.com","/datfile/gc.dat", xml_file, MAX_VERIFY_DAT_SIZE, 0, 0)) > 0) {
			remove(file_path_DAT);
			FILE *fp = fopen(file_path_DAT, "wb");
			if(fp) {
				DrawMessageBox(D_INFO, "Checking for updates\nSaving GC DAT...");
				fwrite(xml_file, 1, res, fp);
				fclose(fp);
				verify_initialized = 0;
				print_gecko("Saved GameCube DAT! %i Bytes\r\n", res);
			}
			else {
				DrawMessageBox(D_FAIL, "Checking for updates\nFailed to save GC DAT...");
				sleep(5);
			}
		}
		else {
			sprintf(txtbuffer, "Error: %i", res);
			print_gecko("Error Saving GC DAT %i\r\n", res);
			DrawMessageBox(D_FAIL, "Checking for updates\nCouldn't find file on gc-forever.com");
			sleep(5);
		}

		// Download the Wii DAT
  		sprintf(file_path_DAT, "%swii.dat",mount_path);
		if((res = http_request("www.gc-forever.com","/datfile/wii.dat", xml_file, MAX_VERIFY_DAT_SIZE, 0, 0)) > 0) {
			remove(file_path_DAT);
			FILE *fp = fopen(file_path_DAT, "wb");
			if(fp) {
				DrawMessageBox(D_INFO, "Checking for updates\nSaving Wii DAT...");
				fwrite(xml_file, 1, res, fp);
				fclose(fp);
				verify_initialized = 0;
				print_gecko("Saved Wii DAT! %i Bytes\r\n", res);
			}	
			else {
				DrawMessageBox(D_FAIL, "Checking for updates\nFailed to save Wii DAT...");
				sleep(5);
			}					
		}
		else {
			sprintf(txtbuffer, "Error: %i", res);
			print_gecko("Error Saving Wii DAT %i\r\n", res);
			DrawMessageBox(D_FAIL, "Checking for updates\nCouldn't find file on gc-forever.com");
			sleep(5);
		}
		free(xml_file);
		ask_DAT_download = 0;
	}
	else {
		ask_DAT_download = 0;
	}
}
#endif

int verify_find_md5(const char *md5, int disc_type) {

	print_gecko("Looking for MD5 [%s]\r\n", md5);

#ifdef HW_RVL
	mxml_node_t *xml_root_node = (disc_type == IS_NGC_DISC)  ? ngc_XML : wii_XML;
#else
	mxml_node_t *xml_root_node = (disc_type == IS_NGC_DISC)  ? ngc_XML : NULL;
#endif
	if (!xml_root_node)
		return 0;

	print_gecko("Looking in the %s XML\r\n", xml_root_node == ngc_XML ? "GameCube" : "Wii");

	// open the <datafile>
	mxml_node_t *datafile_node = mxmlFindElement(xml_root_node, xml_root_node, "datafile", NULL, NULL, MXML_DESCEND);
	if (!datafile_node)
		return 0;
	
	print_gecko("DataFile Pointer OK\r\n");

	// look for md5 attribute in xml directly
	mxml_node_t *rom_node = mxmlFindElement(datafile_node, xml_root_node, NULL, "md5", md5, MXML_DESCEND);
	if (!rom_node)
		return 0; // We didnt find the md5 in the data file

	// we found our rom node with the corresponding md5 attribute in the dat file, look up info about parent node
	mxml_node_t *game_node = mxmlGetParent(rom_node);
	if (!game_node)
		return 0;

	snprintf(&game_name_DAT[0], 128, "%s", mxmlElementGetAttr(game_node, "name"));
	print_gecko("Found a match!\r\n");
	return 1;
}

char *verify_get_name(void) {
	if(strlen(&game_name_DAT[0]) > 32) {
		 game_name_DAT[30] = '.';
		 game_name_DAT[31] = '.';
		 game_name_DAT[32] = 0;
	 }
	return &game_name_DAT[0];
}

int verify_is_available(int disc_type) {
#ifdef HW_RVL
	return (disc_type == IS_NGC_DISC) ? (ngc_DAT != NULL) : (wii_DAT != NULL);
#else
	return (disc_type == IS_NGC_DISC) ? (ngc_DAT != NULL) : 0;
#endif
}
