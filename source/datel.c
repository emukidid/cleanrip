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
static char *datelDAT = NULL;
static int datel_initialized = 0;
static int datelDontAskAgain = 0;
static int SkipFill = 0;
static int NumSkips = 0;
#define MAX_SKIPS (0x580)
static uint64_t SkipStart[MAX_SKIPS];
static uint64_t SkipStop[MAX_SKIPS];

// XML stuff
static mxml_node_t *datelXML = NULL;
static char gameName[256];

void datel_init(char *mountPath) {
	if (datel_initialized) {
		return;
	}
	
	if(datelDAT) {
		if(datelXML) {
			mxmlDelete(datelXML);
			free(datelXML);
		}
		free(datelDAT);
	}

	mxmlSetErrorCallback(print_gecko);
	FILE *fp = NULL;
	// Check for the datel DAT and read it
	sprintf(txtbuffer, "%sdatel.dat", mountPath);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (size > 0) {
			datelDAT = (char*) memalign(32, size);
			if (datelDAT) {
				fread(datelDAT, 1, size, fp);
			}		
		}
		fclose(fp);
		fp = NULL;
	}

	if (datelDAT) {
		datelXML = mxmlLoadString(NULL, datelDAT, MXML_TEXT_CALLBACK);
	}

	print_gecko("DAT Files [Datel: %s]\r\n", datelDAT ? "YES":"NO");
	datel_initialized = (datelDAT&&datelXML);
}

// If there was some new files obtained, return 1, else 0
void datel_download(char *mountPath) {
	if(datelDontAskAgain) {
		return;
	}
	
	int res = 0;
	// Ask the user if they want to update from the web
	if(datel_initialized) {
		char *line1 = "GitHUB.org DAT file found";
		char *line2 = "Check for updated DAT file?";
		res = DrawYesNoDialog(line1, line2);
	}
	else {
		char *line1 = "GitHUB.org DAT files not found";
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
		char datFilePath[64];
  		sprintf(datFilePath, "%sdatel.dat",mountPath);
  		u8 *xmlFile = (u8*)memalign(32, 1*1024*1024);
		if((res = http_request("www.gc-forever.com","/datfile/datel.dat", xmlFile, (1*1024*1024), 0, 0)) > 0) {
			remove(datFilePath);
			FILE *fp = fopen(datFilePath, "wb");
			if(fp) {
				DrawMessageBox(D_INFO, "Checking for updates\nSaving GC DAT...");
				fwrite(xmlFile, 1, res, fp);
				fclose(fp);
				datel_initialized = 0;
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
		datelDontAskAgain = 1;
	}
	else {
		datelDontAskAgain = 1;
	}
}

int datel_findCrcSum(int crcorig) {

	NumSkips = 0;
	print_gecko("Looking for CRC [%x]\r\n", crcorig);
	char *xmlPointer = datelDAT;
	if(xmlPointer) {
		mxml_node_t *pointer = datelXML;
		
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

					SkipFill = strtoul(crc, NULL, 16);
					//print_gecko("Comparing game [%x] and crc [%x]\r\n",mxmlElementGetAttr(nameElem, "name"),mxmlElementGetAttr(crcElem, "crc100000"));
					if (crcval == crcorig) {
						snprintf(&gameName[0], 128, "%s", mxmlElementGetAttr(
								nameElem, "name"));
						DrawYesNoDialog("Found datel crc", gameName);
						print_gecko("Found a match!\r\n");
				mxml_index_t *skipiterator = mxmlIndexNew(gameElem, "skip", NULL);
				mxml_node_t *skipElem = NULL;

				//print_gecko("Item Pointer OK\r\n");
				// iterate over all the <game> entries
				while ((skipElem = mxmlIndexEnum(skipiterator)) != NULL) {
					if (NumSkips >= MAX_SKIPS)
						DrawYesNoDialog("datel crc", "TODO: Too many skips.  Fix source code.");
					char skipstr[64];
					memset(&skipstr[0], 0, 64);
					strncpy(&skipstr[0], mxmlElementGetAttr(skipElem, "start"), 32);

					SkipStart[NumSkips] = strtoull(skipstr, NULL, 16);

					memset(&skipstr[0], 0, 64);
					strncpy(&skipstr[0], mxmlElementGetAttr(skipElem, "stop"), 32);

					SkipStop[NumSkips] = strtoull(skipstr, NULL, 16);
					NumSkips++;
				}
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

void datel_adjustStartStop(uint64_t* start, int* length, int* fill) {
	int n=0;
	*fill = SkipFill;
	for (n=0; (n<NumSkips) && (*length > 0); n++) {
		if ((SkipStart[n] <= *start) && (SkipStop[n] >= *start)) {
			if (SkipStop[n] + 1 > *start + *length)
				*length = 0;
			else {
				*length -= SkipStop[n] + 1 - *start;
				*start = SkipStop[n] + 1;
			}
		}
		if ((SkipStart[n] < (*start + *length)) && (SkipStop[n] >= (*start + *length - 1)) && (*length > 0))
			*length = SkipStart[n] - *start;
	}
}

void datel_addSkip(uint64_t start, int length) {
	if ((NumSkips > 0) && (start == SkipStop[NumSkips-1] + 1))
		SkipStop[NumSkips-1] += length;
	else {
		SkipStart[NumSkips] = start;
		SkipStop[NumSkips] = start + length - 1;
		NumSkips++;
		if (NumSkips == MAX_SKIPS)
			NumSkips=0;
	}
}

void dump_skips(char *mountPath, int crc100000) {
	sprintf(txtbuffer, "%s%s.skp", mountPath, get_game_name());
	FILE *fp = fopen(txtbuffer, "wb");
	if (fp) {
		int sk=0;
		char SkipsInfo[100];
		sprintf(SkipsInfo, "<skipcrc crc100000=""%08x"" skipfill=""%02x""/>\n", crc100000, SkipFill);
		fwrite(SkipsInfo, 1, strlen(&SkipsInfo[0]), fp);
		for (sk=0;sk<NumSkips;sk++) {
			sprintf(SkipsInfo, "<skip start=""%08x"" stop=""%08x""/>\n", (int)(SkipStart[sk] & 0xFFFFFFFF), (int)(SkipStop[sk] & 0xFFFFFFFF));
			fwrite(SkipsInfo, 1, strlen(&SkipsInfo[0]), fp);
		}
		fclose(fp);
	}
}


int datel_is_available() {
	return datelDAT != NULL;
}
