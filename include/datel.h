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

void datel_init(char *mount_path);
int datel_find_crc_sum(int crc);
int datel_is_available(void);
int datel_download(char *mount_path);
void datel_adjust_start_stop(uint64_t *start, u32 *length, u32 *fill);
void datel_add_skip(uint64_t start, u32 length);
void datel_write_dump_skips(char *mount_path, u32 crc100000);

#endif
