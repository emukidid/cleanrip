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

// Pointers to the two files
static char *ngcDAT = NULL;
static char *wiiDAT = NULL;
static int verify_initialized = 0;
int net_initialized = 0;
static int dontAskAgain = 0;

// XML stuff
static mxml_node_t *ngcXML = NULL;
static mxml_node_t *wiiXML = NULL;
static char gameName[256];

void verify_init(char *mountPath) {
	if (verify_initialized) {
		return;
	}
	
	if(ngcDAT) {
		if(ngcXML) {
			mxmlDelete(ngcXML);
			free(ngcXML);
		}
		free(ngcDAT);
	}
	if(wiiDAT) {
		free(wiiDAT);
		if(wiiXML) {
			mxmlDelete(wiiXML);
			free(wiiXML);
		}
	}

	mxmlSetErrorCallback((mxml_error_cb_t)print_gecko);
	FILE *fp = NULL;
	// Check for the Gamecube Redump.org DAT and read it
	sprintf(txtbuffer, "%sgc.dat", mountPath);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (size > 0) {
			ngcDAT = (char*) memalign(32, size);
			if (ngcDAT) {
				fread(ngcDAT, 1, size, fp);
			}		
		}
		fclose(fp);
		fp = NULL;
	}

	if (ngcDAT) {
		ngcXML = mxmlLoadString(NULL, ngcDAT, MXML_OPAQUE_CALLBACK);
	}

#ifdef HW_RVL
	// Check for the Wii Redump.org DAT and read it
	sprintf(txtbuffer, "%swii.dat", mountPath);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		fseek(fp, 0L, SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (size > 0) {
			wiiDAT = (char*) memalign(32, size);
			if (wiiDAT) {
				fread(wiiDAT, 1, size, fp);
			}
		}	
		fclose(fp);
		fp = NULL;
	}
	
	if (wiiDAT) {
		wiiXML = mxmlLoadString(NULL, wiiDAT, MXML_OPAQUE_CALLBACK);
	}
#endif // #ifdef HW_RVL

	print_gecko("DAT Files [NGC: %s] [Wii: %s]\r\n", ngcDAT ? "YES":"NO", wiiDAT ? "YES":"NO");
	verify_initialized = ((ngcDAT&&ngcXML)
#ifdef HW_RVL
		&& (wiiDAT&&wiiXML)
#endif // #ifdef HW_RVL
		);
}

// If there was some new files obtained, return 1, else 0
void verify_download(char *mountPath) {
	if(dontAskAgain) {
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
			res = if_config(ip, NULL, NULL, true, 3);
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
  		sprintf(datFilePath, "%sgc.dat",mountPath);
  		u8 *xmlFile = (u8*)memalign(32, 3*1024*1024);
		if((res = http_request("www.gc-forever.com","/datfile/gc.dat", xmlFile, (3*1024*1024), 0, 0)) > 0) {
			remove(datFilePath);
			FILE *fp = fopen(datFilePath, "wb");
			if(fp) {
				DrawMessageBox(D_INFO, "Checking for updates\nSaving GC DAT...");
				fwrite(xmlFile, 1, res, fp);
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

#ifdef HW_RVL
		// Download the Wii DAT
  		sprintf(datFilePath, "%swii.dat",mountPath);
		if((res = http_request("www.gc-forever.com","/datfile/wii.dat", xmlFile, (3*1024*1024), 0, 0)) > 0) {
			remove(datFilePath);
			FILE *fp = fopen(datFilePath, "wb");
			if(fp) {
				DrawMessageBox(D_INFO, "Checking for updates\nSaving Wii DAT...");
				fwrite(xmlFile, 1, res, fp);
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
#endif // #ifdef HW_RVL
		free(xmlFile);
		dontAskAgain = 1;
	}
	else {
		dontAskAgain = 1;
	}
}

int verify_findMD5Sum(const char * md5orig, int disc_type) {

	print_gecko("Looking for MD5 [%s]\r\n", md5orig);

	mxml_node_t *pointer = (disc_type == IS_NGC_DISC)  ? ngcXML : wiiXML;
	if (!pointer)
		return 0;

	print_gecko("Looking in the %s XML\r\n", pointer == ngcXML ? "GameCube" : "Wii");

	// open the <datafile>
	mxml_node_t *item = mxmlFindElement(pointer, pointer, "datafile", NULL, NULL, MXML_DESCEND);
	if (!item)
		return 0;
	
	print_gecko("DataFile Pointer OK\r\n");

	// look for md5 in xml directly
	mxml_node_t *md5Elem = mxmlFindElement(item, pointer, NULL, "md5", md5orig, MXML_DESCEND);
	if (!md5Elem)
		return 0; // We didnt find the md5 in the data file

	// we found our md5 in the dat file, look up info about parent node
	mxml_node_t *gameElem = mxmlGetParent(md5Elem);
	if (!gameElem)
		return 0;

	snprintf(&gameName[0], 128, "%s", mxmlElementGetAttr(gameElem, "name"));
	print_gecko("Found a match!\r\n");
	return 1;
}

char *verify_get_name() {
	if(strlen(&gameName[0]) > 32) {
		 gameName[30] = '.';
		 gameName[31] = '.';
		 gameName[32] = 0;
	 }
	return &gameName[0];
}

int verify_is_available(int disc_type) {
	return (disc_type == IS_NGC_DISC) ? (ngcDAT != NULL) : (wiiDAT != NULL);
}
