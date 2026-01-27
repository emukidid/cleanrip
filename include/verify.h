/**
 * CleanRip - verify.h
 * Copyright (C) 2010-2026 emu_kidid
 *
 * CleanRip homepage: https://github.com/emukidid/cleanrip
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

#ifndef VERIFY_H
#define VERIFY_H

extern int net_initialized;

enum {
	VERIFY_INTERNAL_CRC=0,
	VERIFY_REDUMP_DAT_GC,
	VERIFY_REDUMP_DAT_WII
};

void verify_init(const char *mountPath);
int verify_findCrc32(u32 crc32, int disc_type);
int verify_findMD5Sum(const char * md5, int disc_type);
int verify_is_available(int disc_type);
void verify_download(const char *mountPath);
char *verify_get_name(int flag);
char *verify_get_internal_updated(int disc_type);

#endif

