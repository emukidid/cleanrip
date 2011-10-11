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
#include "gczip.h"
#include "main.h"

// Pointers to the two files
static char *ngcDAT = NULL;
static int verify_initialized = 0;

// XML stuff
static mxml_node_t *ngcXML = NULL;
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
		ngcXML = mxmlLoadString(NULL, ngcDAT, MXML_TEXT_CALLBACK);
	}

	verify_initialized = (ngcDAT != NULL);
}

int verify_findMD5Sum(const char * md5orig) {
	
	char *xmlPointer = ngcDAT;
	if(xmlPointer) {
		mxml_node_t *pointer = ngcXML;
		
		pointer = mxmlLoadString(NULL, xmlPointer, MXML_TEXT_CALLBACK);

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
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

char *verify_get_name() {
	if(strlen(&gameName[0]) > 32) {
		 gameName[30] = '.';
		 gameName[31] = '.';
		 gameName[32] = 0;
	 }
	return &gameName[0];
}

int verify_is_available() {
	return(ngcDAT != NULL);
}
