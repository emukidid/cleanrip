/**
 * CleanRip - gc_dvd.h (originally from Cube64/Wii64)
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

#ifndef GC_DVD_H
#define GC_DVD_H

#include <stdint.h>
//Used in ISO9660 Parsing
#define NO_FILES -1
#define NO_ISO9660_DISC -2
#define FATAL_ERROR -3
#define MAXIMUM_ENTRIES_PER_DIR 512

#define GC_CPU_VERSION 0x00083214
#define NO_HW_ACCESS -1000
#define NO_DISC      -1001
#define NORMAL 0xA8000000
#define DVDR   0xD0000000

int dvd_initialise_drive(void);
void dvd_motor_off(void);
u32 dvd_get_error(void);
char *dvd_error_str(void);
int dvd_read_id(void);
void dvd_read_bca(void* dst);
int dvd_low_read_64(void* dst, u32 len, uint64_t offset);
int dvd_low_read_64_datel(void* dst, u32 len, uint64_t offset, int is_known_datel);
void dvd_xeno_disable(void);
#endif

