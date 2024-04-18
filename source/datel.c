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

// Pointers to the file
static char *datel_DAT = NULL;
static int is_datel_initialized = 0;
static int skip_fill_value = 0;
static int num_skips = 0;
#define MAX_SKIPS (0x580)
static uint64_t skip_start[MAX_SKIPS];
static uint64_t skip_stop[MAX_SKIPS];
#ifdef HW_RVL
static int datel_dont_ask_again = 0;
#endif

// XML stuff
static mxml_node_t *datel_XML = NULL;
static char game_name[256];

void datel_init(char *mount_path) {
	if (is_datel_initialized) {
		return;
	}
	
	if(datel_DAT) {
		if(datel_XML) {
			mxmlDelete(datel_XML);
			free(datel_XML);
		}
		free(datel_DAT);
	}

	mxmlSetErrorCallback((mxml_error_cb_t)print_gecko);
	FILE *fp = NULL;
	// Check for the datel DAT and read it
	sprintf(txtbuffer, "%sdatel.dat", mount_path);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		int file_size_DAT = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (file_size_DAT > 0) {
			datel_DAT = (char*) memalign(32, file_size_DAT);
			if (datel_DAT) {
				fread(datel_DAT, 1, file_size_DAT, fp);
			}		
		}
		fclose(fp);
		fp = NULL;
	}

	if (datel_DAT) {
		datel_XML = mxmlLoadString(NULL, datel_DAT, MXML_TEXT_CALLBACK);
	}

	print_gecko("DAT Files [Datel: %s]\r\n", datel_DAT ? "YES":"NO");
	is_datel_initialized = (datel_DAT&&datel_XML);
}

#ifdef HW_RVL
// If there was some new files obtained, return 1, else 0
void datel_download(char *mount_path) {
	if(datel_dont_ask_again) {
		return;
	}
	
	int res = 0;
	// Ask the user if they want to update from the web
	if(is_datel_initialized) {
		char *line1 = "gc-forever Datel DAT file found";
		char *line2 = "Check for updated DAT file?";
		res = DrawYesNoDialog(line1, line2);
	}
	else {
		char *line1 = "gc-forever Datel DAT files not found";
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

  		// Download the GC DAT
		char file_path_DAT[64];
  		sprintf(file_path_DAT, "%sdatel.dat",mount_path);
  		u8 *xmlFile = (u8*)memalign(32, 1*1024*1024);
		if((res = http_request("www.gc-forever.com","/datfile/datel.dat", xmlFile, (1*1024*1024), 0, 0)) > 0) {
			remove(file_path_DAT);
			FILE *fp = fopen(file_path_DAT, "wb");
			if(fp) {
				DrawMessageBox(D_INFO, "Checking for updates\nSaving GC DAT...");
				fwrite(xmlFile, 1, res, fp);
				fclose(fp);
				is_datel_initialized = 0;
				print_gecko("Saved GameCube DAT! %i Bytes\r\n", res);
			}
			else {
				DrawMessageBox(D_FAIL, "Checking for updates\nFailed to save Datel DAT...");
				sleep(5);
			}
		}
		else {
			sprintf(txtbuffer, "Error: %i", res);
			print_gecko("Error Saving Datel DAT %i\r\n", res);
			DrawMessageBox(D_FAIL, "Checking for updates\nCouldn't find file on gc-forever.com");
			sleep(5);
		}
		free(xmlFile);
		datel_dont_ask_again = 1;
	}
	else {
		datel_dont_ask_again = 1;
	}
}
#endif

int datel_find_crc_sum(int crcorig) {

	num_skips = 0;
	print_gecko("Looking for CRC [%x]\r\n", crcorig);
	char *xmlPointer = datel_DAT;
	if(xmlPointer) {
		mxml_node_t *pointer = datel_XML;
		
		pointer = mxmlLoadString(NULL, xmlPointer, MXML_TEXT_CALLBACK);
		
		print_gecko("Looking in the Datel XML\r\n");
		if (pointer) {
			// open the <datafile>
			mxml_node_t *item = mxmlFindElement(pointer, pointer, "datafile", NULL,
					NULL, MXML_DESCEND);
			print_gecko("DataFile Pointer OK\r\n");
			if (item) {
				mxml_index_t *iterator = mxmlIndexNew(item, "game", NULL);
				mxml_node_t *gameElem = NULL;

				//print_gecko("Item Pointer OK\r\n");
				// iterate over all the <game> entries
				while ((gameElem = mxmlIndexEnum(iterator)) != NULL) {
					// get the crc and compare it
					mxml_node_t *crcElem = mxmlFindElement(gameElem, gameElem,
							NULL, "crc100000", NULL, MXML_DESCEND);
					// get the name too
					mxml_node_t *nameElem = mxmlFindElement(gameElem, gameElem,
							NULL, "name", NULL, MXML_DESCEND);
					mxml_node_t *fillElem = mxmlFindElement(gameElem, gameElem,
							NULL, "skipfill", NULL, MXML_DESCEND);

					char crc[64];
					memset(&crc[0], 0, 64);
					strncpy(&crc[0], mxmlElementGetAttr(crcElem, "crc100000"), 32);

					int crcval = strtoul(crc, NULL, 16);
					if (!strncmp(crc, "default", 7))
						crcval = crcorig;

					memset(&crc[0], 0, 64);
					strncpy(&crc[0], mxmlElementGetAttr(fillElem, "skipfill"), 32);

					skip_fill_value = strtoul(crc, NULL, 16);
					//print_gecko("Comparing game [%x] and crc [%x]\r\n",mxmlElementGetAttr(nameElem, "name"),mxmlElementGetAttr(crcElem, "crc100000"));
					if (crcval == crcorig) {
						snprintf(&game_name[0], 128, "%s", mxmlElementGetAttr(
								nameElem, "name"));
						print_gecko("Found a match!\r\n");
				mxml_index_t *skipiterator = mxmlIndexNew(gameElem, "skip", NULL);
				mxml_node_t *skipElem = NULL;

				//print_gecko("Item Pointer OK\r\n");
				// iterate over all the <game> entries
				while ((skipElem = mxmlIndexEnum(skipiterator)) != NULL) {
					if (num_skips >= MAX_SKIPS)
						DrawYesNoDialog("datel crc", "TODO: Too many skips.  Fix source code.");
					char skipstr[64];
					memset(&skipstr[0], 0, 64);
					strncpy(&skipstr[0], mxmlElementGetAttr(skipElem, "start"), 32);

					skip_start[num_skips] = strtoull(skipstr, NULL, 16);

					memset(&skipstr[0], 0, 64);
					strncpy(&skipstr[0], mxmlElementGetAttr(skipElem, "stop"), 32);

					skip_stop[num_skips] = strtoull(skipstr, NULL, 16);
					num_skips++;
				}
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

void datel_adjust_start_stop(uint64_t *start, u32 *length, u32 *fill) {
	int n=0;
	*fill = skip_fill_value;
	for (n=0; (n<num_skips) && (*length > 0); n++) {
		if ((skip_start[n] <= *start) && (skip_stop[n] >= *start)) {
			if (skip_stop[n] + 1 > *start + *length)
				*length = 0;
			else {
				*length -= skip_stop[n] + 1 - *start;
				*start = skip_stop[n] + 1;
			}
		}
		if ((skip_start[n] < (*start + *length)) && (skip_stop[n] >= (*start + *length - 1)) && (*length > 0))
			*length = skip_start[n] - *start;
	}
}

void datel_add_skip(uint64_t start, u32 length) {
	if ((num_skips > 0) && (start == skip_stop[num_skips-1] + 1))
		skip_stop[num_skips-1] += length;
	else {
		skip_start[num_skips] = start;
		skip_stop[num_skips] = start + length - 1;
		num_skips++;
		if (num_skips == MAX_SKIPS)
			num_skips=0;
	}
}

void datel_write_dump_skips(char *mount_path, u32 crc100000) {
	sprintf(txtbuffer, "%s%s.skp", mount_path, get_game_name());
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		int sk=0;
		char skips_info[100];
		sprintf(skips_info, "\t\t<skipcrc crc100000=\"%08X\" skipfill=\"%02X\"/>\n", crc100000, skip_fill_value);
		fwrite(skips_info, 1, strlen(&skips_info[0]), fp);
		for (sk=0;sk<num_skips;sk++) {
			sprintf(skips_info, "\t\t<skip start=\"%08X\" stop=\"%08X\"/>\n", (u32)(skip_start[sk] & 0xFFFFFFFF), (u32)(skip_stop[sk] & 0xFFFFFFFF));
			fwrite(skips_info, 1, strlen(&skips_info[0]), fp);
		}
		fclose(fp);
	}
}


int datel_is_available(void) {
	return datel_DAT != NULL;
}
