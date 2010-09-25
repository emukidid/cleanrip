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
#include "gczip.h"
#include "http.h"
#include "main.h"

// Pointers to the two files
static char *ngcDAT = NULL;
static char *wiiDAT = NULL;
static int verify_initialized = 0;
static int net_initialized = 0;
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
	
	if (ngcDAT) {
		ngcXML = mxmlLoadString(NULL, ngcDAT, MXML_TEXT_CALLBACK);
	}
	if (wiiDAT) {
		wiiXML = mxmlLoadString(NULL, wiiDAT, MXML_TEXT_CALLBACK);
	}

	verify_initialized = (ngcDAT && wiiDAT);
}

// If there was some new files obtained, return 1, else 0
void verify_download(char *mountPath) {
	if(dontAskAgain) {
		return;
	}
	
	int res = 0;
	// Ask the user if they want to update from the web
	if(verify_initialized) {
		char *line1 = "Redump.org DAT files found";
		char *line2 = "Check for updated DAT files?";
		res = DrawYesNoDialog(line1, line2);
	}
	else {
		char *line1 = "Redump.org DAT files not found";
		char *line2 = "Download them now?";
		res = DrawYesNoDialog(line1, line2);
	}
	
	// If yes, lets download an update
	if(res) {
		// Initialize the network
		if(!net_initialized) {
			char ip[16];
			DrawMessageBox("Checking for DAT updates",NULL,"Initializing Network...",NULL);
			res = if_config(ip, NULL, NULL, true);
      		if(res >= 0) {
	      		sprintf(txtbuffer, "IP: %s", ip);
	      		DrawMessageBox("Checking for DAT updates","Network Initialized!",NULL,txtbuffer);
				net_initialized = 1;
			}
      		else {
	      		DrawMessageBox("Checking for DAT updates",NULL,"Network failed to Initialize!",NULL);
	      		sleep(5);
        		net_initialized = 0;
        		return;
      		}
  		}
  		
  		
  		u8 *zipFile = (u8*)memalign(32, 1*1024*1024);
  		if(zipFile) {
	  		// Download the GC DAT
  			char datFilePath[64];
	  		sprintf(datFilePath, "%sgc.dat",mountPath);
	  		u8 *xmlFile = (u8*)memalign(32, 3*1024*1024);
			if((res = http_request("redump.org","/datfile/gc/", zipFile, (1*1024*1024), 0, 0)) > 0) {
				PKZIPHEADER pkzip;
				memcpy(&pkzip, zipFile, sizeof(PKZIPHEADER));
				if(pkzip.zipid == PKZIPID) {		//PKZIP magic
					int uncompressedSize = FLIP32(pkzip.uncompressedSize);
					inflate_init(&pkzip);
					DrawMessageBox("Checking for updates",NULL,"Extracting GC DAT...",NULL);
					res = inflate_chunk(xmlFile, zipFile, res, uncompressedSize);
					remove(datFilePath);
					FILE *fp = fopen(datFilePath, "wb");
					if(fp) {
						DrawMessageBox("Checking for updates",NULL,"Saving GC DAT...",NULL);
						fwrite(xmlFile, 1, uncompressedSize, fp);
						fclose(fp);
						verify_initialized = 0;
					}
					else {
						DrawMessageBox("Checking for updates",NULL,"Failed to save GC DAT...",NULL);
						sleep(5);
					}					
				} else {
					DrawMessageBox("Checking for updates",NULL,"Invalid ZIP file found",NULL);
					sleep(5);
				}
			}
			else {
				sprintf(txtbuffer, "Error: %i", res);
				DrawMessageBox("Checking for updates",NULL,"Couldn't find file on redump.org",txtbuffer);
				sleep(5);
			}
			// Download the Wii DAT
  			sprintf(datFilePath, "%swii.dat",mountPath);
			if((res = http_request("redump.org","/datfile/wii/", zipFile, (1*1024*1024), 0, 0)) > 0) {
				PKZIPHEADER pkzip;
				memcpy(&pkzip, zipFile, sizeof(PKZIPHEADER));
				if(pkzip.zipid == PKZIPID) {		//PKZIP magic
					int uncompressedSize = FLIP32(pkzip.uncompressedSize);
					inflate_init(&pkzip);
					DrawMessageBox("Checking for updates",NULL,"Extracting Wii DAT...",NULL);
					res = inflate_chunk(xmlFile, zipFile, res, uncompressedSize);
					remove(datFilePath);
					FILE *fp = fopen(datFilePath, "wb");
					if(fp) {
						DrawMessageBox("Checking for updates",NULL,"Saving Wii DAT...",NULL);
						fwrite(xmlFile, 1, uncompressedSize, fp);
						fclose(fp);
						verify_initialized = 0;
					}	
					else {
						DrawMessageBox("Checking for updates",NULL,"Failed to save Wii DAT...",NULL);
						sleep(5);
					}					
				} else {
					DrawMessageBox("Checking for updates",NULL,"Invalid ZIP file found",NULL);
					sleep(5);
				}
			}
			else {
				sprintf(txtbuffer, "Error: %i", res);
				DrawMessageBox("Checking for updates",NULL,"Couldn't find file on redump.org",txtbuffer);
				sleep(5);
			}
			free(xmlFile);
			free(zipFile);
			dontAskAgain = 1;
        } 
         else {
			DrawMessageBox("Checking for updates",NULL,"Failed to create save buffer!",NULL);
			sleep(5);
        }
        
	}
	else {
		dontAskAgain = 1;
	}

	return;
}

int verify_findMD5Sum(const char * md5orig, int disc_type) {
	
	char *xmlPointer = (disc_type == IS_NGC_DISC) ? ngcDAT : wiiDAT;
	if(xmlPointer) {
		mxml_node_t *pointer = (disc_type == IS_NGC_DISC)  ? ngcXML : wiiXML;
		
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

int verify_is_available(int disc_type) {
	return (disc_type == IS_NGC_DISC) ? (ngcDAT != NULL) : (wiiDAT != NULL);
}
