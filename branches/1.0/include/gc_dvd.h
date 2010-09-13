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


typedef struct
{
	char name[128];
	int flags;
	int sector, size;
} file_entry; 

typedef struct
{
	file_entry file[MAXIMUM_ENTRIES_PER_DIR];
} file_entries; 

extern file_entries DVDToc;

int init_dvd();
void dvd_motor_off();
unsigned int dvd_get_error(void);
int dvd_read_directoryentries(uint64_t offset, int size);
void read_directory(int sector, int len);
int read_safe(void* dst, uint64_t offset, int len);
int read_direntry(unsigned char* direntry);
int read_sector(void* buffer, uint32_t sector);
int dvd_read(void* dst,unsigned int len, unsigned int offset);
int dvd_read_id();
int DVD_LowRead64(void* dst, unsigned int len, uint64_t offset);

struct pvd_s
{
	char id[8];
	char system_id[32];
	char volume_id[32];
	char zero[8];
	unsigned long total_sector_le, total_sect_be;
	char zero2[32];
	unsigned long volume_set_size, volume_seq_nr;
	unsigned short sector_size_le, sector_size_be;
	unsigned long path_table_len_le, path_table_len_be;
	unsigned long path_table_le, path_table_2nd_le;
	unsigned long path_table_be, path_table_2nd_be;
	unsigned char root_direntry[34];
	char volume_set_id[128], publisher_id[128], data_preparer_id[128], application_id[128];
	char copyright_file_id[37], abstract_file_id[37], bibliographical_file_id[37];
	// some additional dates, but we don't care for them :)
}  __attribute__((packed));

#endif

