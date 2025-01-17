/**
 * CleanRip - datel.h
 * Copyright (C) 2010 emu_kidid
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

#ifndef DATEL_H
#define DATEL_H

void datel_init(char *mountPath);
int datel_findCrcSum(int crc);
int datel_is_available(int disc_type);
int datel_download(char *mountPath);
char *datel_get_name();
void datel_adjustStartStop(uint64_t* start, u32* length, u32* fill);
void datel_addSkip(uint64_t start, u32 length);
void dump_skips(char *mountPath, u32 crc100000);
int datel_findMD5Sum(const char *md5orig);
char* datel_get_name(int flag);

#endif

