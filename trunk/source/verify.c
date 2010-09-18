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
#include <mxml.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "main.h"

// Pointers to the two files
static char *ngcDAT = NULL;
static char *wiiDAT = NULL;
static int verify_initialized = 0;

// XML stuff
static mxml_node_t *ngcXML = NULL;
static mxml_node_t *wiiXML = NULL;
static char gameName[256];

void verify_init(char *mountPath) {
	if (verify_initialized) {
		return;
	}

	FILE *fp = NULL;
	// Check for the Gamecube Redump.org DAT and read it
	sprintf(txtbuffer, "%sgc.dat", mountPath);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		struct stat fileInfo;
		if (!stat(txtbuffer, &fileInfo)) {
			if (fileInfo.st_size > 0) {
				ngcDAT = (char*) memalign(32, fileInfo.st_size);
				if (ngcDAT) {
					fread(ngcDAT, 1, fileInfo.st_size, fp);
				}
			}
		}
		fclose(fp);
		fp = NULL;
	}

	// Check for the Wii Redump.org DAT and read it
	sprintf(txtbuffer, "%swii.dat", mountPath);
	fp = fopen(txtbuffer, "rb");
	if (fp) {
		struct stat fileInfo;
		if (!stat(txtbuffer, &fileInfo)) {
			if (fileInfo.st_size > 0) {
				wiiDAT = (char*) memalign(32, fileInfo.st_size);
				if (wiiDAT) {
					fread(wiiDAT, 1, fileInfo.st_size, fp);
				}
			}
		}
		fclose(fp);
		fp = NULL;
	}

	if (ngcDAT) {
		ngcXML = mxmlLoadString(NULL, ngcDAT, MXML_TEXT_CALLBACK);
	}
	if (wiiDAT) {
		wiiXML = mxmlLoadString(NULL, wiiDAT, MXML_TEXT_CALLBACK);
	}

	verify_initialized = 1;
}

int verify_findMD5Sum(const char * md5orig, int disc_type) {
	mxml_node_t *pointer = (disc_type == IS_NGC_DISC) ? ngcXML : wiiXML;

	if (pointer) {
		// open the <datafile>
		mxml_node_t *item = mxmlFindElement(pointer, pointer, "datafile", NULL,
				NULL, MXML_DESCEND);
		if (item) {
			mxml_index_t *iterator = mxmlIndexNew(item, "game", NULL);
			mxml_node_t *gameElem = NULL;

			// iterate over all the <game> entries
			while ((gameElem = mxmlIndexEnum(iterator)) != NULL) {
				// get the md5 and compare it
				mxml_node_t *md5Elem = mxmlFindElement(gameElem, gameElem,
						NULL, "md5", NULL, MXML_DESCEND);
				// get the name too
				mxml_node_t *nameElem = mxmlFindElement(gameElem, gameElem,
						NULL, "name", NULL, MXML_DESCEND);

				char md5[64];
				memset(&md5[0], 0, 64);
				strncpy(&md5[0], mxmlElementGetAttr(md5Elem, "md5"), 32);
				
				if (!strnicmp(&md5[0], md5orig, 32)) {
					snprintf(&gameName[0], 128, "%s", mxmlElementGetAttr(
							nameElem, "name"));
					mxmlDelete(md5Elem);
					mxmlDelete(nameElem);
					mxmlDelete(gameElem);
					mxmlDelete(item);
					
					return 1;
				}
			}
			mxmlDelete(item);
		}
	}

	return 0;
}

char *verify_get_name() {
	return &gameName[0];
}

int verify_is_available(int disc_type) {
	return (disc_type == IS_NGC_DISC) ? (ngcXML != NULL) : (wiiXML != NULL);
}
