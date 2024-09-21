/**
 * CleanRip - verify.h
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

#ifndef VERIFY_H
#define VERIFY_H

#define MAX_VERIFY_DAT_SIZE (3*1024*1024)

extern int net_initialized;

void verify_initialise(char *mount_path);
int verify_find_md5(const char *md5, int disc_type);
int verify_is_available(int disc_type);
void verify_download_DAT(char *mount_path);
char *verify_get_name(void);

#endif

