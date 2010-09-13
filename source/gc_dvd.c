/**
 * CleanRip - gc_dvd.c (originally from Cube64/Wii64)
 * Copyright (C) 2007, 2008, 2009, 2010 emu_kidid
 * 
 * DVD Reading support for GC/Wii
 *
 * CleanRip homepage: http://code.google.com/p/cleanrip
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ogc/dvd.h>
#include <malloc.h>
#include <string.h>
#include <gccore.h>
#include <unistd.h>
#include <di/di.h>
#include "gc_dvd.h"

#ifdef WII
#include <di/di.h>
#endif

/* DVD Stuff */
u32 dvd_hard_init = 0;
static u32 read_cmd = NORMAL;
static int last_current_dir = -1;
int is_unicode,files;
file_entries DVDToc;

#ifdef HW_DOL
#define mfpvr()   ({unsigned int rval; \
      asm volatile("mfpvr %0" : "=r" (rval)); rval;})
#endif
#ifdef HW_RVL
volatile unsigned long* dvd = (volatile unsigned long*)0xCD806000;
#else
volatile unsigned long* dvd = (volatile unsigned long*)0xCC006000;
#endif

int init_dvd() {
// Gamecube Mode
#ifdef HW_DOL
  if(mfpvr()!=GC_CPU_VERSION) //GC mode on Wii, modchip required
  {
    DVD_Reset(DVD_RESETHARD);
    dvd_read_id();
    if(!dvd_get_error()) {
      return 0; //we're ok
    }
  }
  else      //GC, no modchip even required :)
  {
    DVD_Reset(DVD_RESETHARD);
    DVD_Mount ();
    if(!dvd_get_error()) {
      return 0; //we're ok
    }
  }
  if(dvd_get_error()>>24) {
    return NO_DISC;
  }
  return -1;

#endif
// Wii (Wii mode)
#ifdef HW_RVL
  if(!dvd_hard_init) {
    DI_Init();
  }
  if((dvd_get_error()>>24) == 1) {
    return NO_DISC;
  }
  
  if((!dvd_hard_init) || (dvd_get_error())) {
    DI_Mount();
    while(DI_GetStatus() & DVD_INIT) usleep(20000);
    dvd_hard_init=1;
  }

  if((dvd_get_error()&0xFFFFFF)==0x053000) {
    read_cmd = DVDR;
  }
  else {
    read_cmd = NORMAL;
  }
  dvd_read_id();
  return 0;
#endif
}

void dvd_unlock()
{
	dvd[0] |= 0x00000014;
	dvd[1] = 0;
	dvd[2] = 0xFF016D61;
	dvd[3] = 0x74736869;
	dvd[4] = 0x74610200;
	dvd[7] = 1;
	while ((dvd[0] & 0x14) == 0);

	dvd[0] |= 0x00000014;
	dvd[1] = 0;
	dvd[2] = 0xFF006476;
	dvd[3] = 0x642D6761;
	dvd[4] = 0x6D650300;
	dvd[7] = 1;
	while ((dvd[0] & 0x14) == 0);
}

void dvd_setstatus()
{
	dvd[0] = 0x2E;
	dvd[1] = 0;

	dvd[2] = 0xee060300;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // enable reading!
	while (dvd[7] & 1);
}

void dvd_SetExtension()
{
	dvd[0] = 0x2E;
	dvd[1] = 0;

	dvd[2] = 0x55010000;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // enable reading!
	while (dvd[7] & 1);
}

int dvd_read_id()
{
#ifdef HW_RVL
  DVD_LowRead64((void*)0x80000000, 32, 0ULL);  //for easter egg disc support
  return 0;
#endif
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xA8000040;
	dvd[3] = 0;
	dvd[4] = 0x20;
	dvd[5] = 0x80000000;
	dvd[6] = 0x20;
	dvd[7] = 3; // enable reading!
	while (dvd[7] & 1);
	if (dvd[0] & 0x4)
		return 1;
	return 0;
}

unsigned int dvd_get_error(void)
{
	dvd[2] = 0xE0000000;
	dvd[8] = 0;
	dvd[7] = 1; // IMM
	while (dvd[7] & 1);
	return dvd[8];
}


void dvd_motor_off()
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xe3000000;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // IMM
	while (dvd[7] & 1);
}

/* 
DVD_LowRead64(void* dst, unsigned int len, uint64_t offset)
  Read Raw, needs to be on sector boundaries
  Has 8,796,093,020,160 byte limit (8 TeraBytes)
  Synchronous function.
    return -1 if offset is out of range
*/
int DVD_LowRead64(void* dst, unsigned int len, uint64_t offset)
{
  if(offset>>2 > 0xFFFFFFFF)
    return -1;
    
  if ((((int)dst) & 0xC0000000) == 0x80000000) // cached?
		dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = read_cmd;
	dvd[3] = read_cmd == DVDR ? offset>>11 : offset >> 2;
	dvd[4] = read_cmd == DVDR ? len>>11 : len;
	dvd[5] = (unsigned long)dst;
	dvd[6] = len;
	dvd[7] = 3; // enable reading!
	DCInvalidateRange(dst, len);
	while (dvd[7] & 1);

	if (dvd[0] & 0x4)
		return 1;
	return 0;
}

void dvd_set_offset(u64 offset)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xD9000000;
	dvd[3] = ((offset>>2)&0xFFFFFFFF);
	dvd[4] = 0;
	dvd[7] = 1;
	while (dvd[7] & 1);
}

static char error_str[256];
char *dvd_error_str()
{
  unsigned int err = dvd_get_error();
  if(!err) return "OK";
  
  memset(&error_str[0],0,256);
  switch(err>>24) {
    case 0:
      break;
    case 1:
      strcpy(&error_str[0],"Lid open");
      break;
    case 2:
      strcpy(&error_str[0],"No disk/Disk changed");
      break;
    case 3:
      strcpy(&error_str[0],"No disk");
      break;  
    case 4:
      strcpy(&error_str[0],"Motor off");
      break;  
    case 5:
      strcpy(&error_str[0],"Disk not initialized");
      break;
  }
  switch(err&0xFFFFFF) {
    case 0:
      break;
    case 0x020400:
      strcat(&error_str[0]," Motor Stopped");
      break;
    case 0x020401:
      strcat(&error_str[0]," Disk ID not read");
      break;
    case 0x023A00:
      strcat(&error_str[0]," Medium not present / Cover opened");
      break;  
    case 0x030200:
      strcat(&error_str[0]," No Seek complete");
      break;  
    case 0x031100:
      strcat(&error_str[0]," UnRecoverd read error");
      break;
    case 0x040800:
      strcat(&error_str[0]," Transfer protocol error");
      break;
    case 0x052000:
      strcat(&error_str[0]," Invalid command operation code");
      break;
    case 0x052001:
      strcat(&error_str[0]," Audio Buffer not set");
      break;  
    case 0x052100:
      strcat(&error_str[0]," Logical block address out of range");
      break;  
    case 0x052400:
      strcat(&error_str[0]," Invalid Field in command packet");
      break;
    case 0x052401:
      strcat(&error_str[0]," Invalid audio command");
      break;
    case 0x052402:
      strcat(&error_str[0]," Configuration out of permitted period");
      break;
    case 0x053000:
      strcat(&error_str[0]," DVD-R"); //?
      break;
    case 0x053100:
      strcat(&error_str[0]," Wrong Read Type"); //?
      break;
    case 0x056300:
      strcat(&error_str[0]," End of user area encountered on this track");
      break;  
    case 0x062800:
      strcat(&error_str[0]," Medium may have changed");
      break;  
    case 0x0B5A01:
      strcat(&error_str[0]," Operator medium removal request");
      break;
  }
  if(!error_str[0])
    sprintf(&error_str[0],"Unknown %08X",err);
  return &error_str[0];
  
}


int read_sector(void* buffer, uint32_t sector)
{
	return DVD_LowRead64(buffer, 2048, sector * 2048);
}

int read_safe(void* dst, uint64_t offset, int len)
{
	int ret = 0, len_read = 0;
  unsigned char* sector_buffer = (unsigned char*)memalign(32,2048); 
  
  // if required, align the start and read it
  if(offset&2047) {
    int alignment = offset&2047;
    int amount_to_copy = len > 2048-alignment ? 2048-alignment : len-alignment;
    ret |= DVD_LowRead64(sector_buffer, 2048, offset - alignment);
    memcpy(dst, sector_buffer+alignment, amount_to_copy );	

    len_read += amount_to_copy;
		offset += amount_to_copy;
		len -= amount_to_copy;
		dst += amount_to_copy;
  }
  
  //if required, do aligned reads now until the end.
	while (len)
	{
		ret |= DVD_LowRead64(sector_buffer, 2048, offset);
		int amount_to_copy = len > 2048 ? 2048 : len;
		memcpy(dst, sector_buffer, amount_to_copy);	

		len_read += amount_to_copy;
		offset += amount_to_copy;
		len -= amount_to_copy;
		dst += amount_to_copy;
	}
	free(sector_buffer);
	if((ret) || (dvd_get_error())) {
  	return -1;
	}

  return len_read;
}

int read_direntry(unsigned char* direntry)
{
       int nrb = *direntry++;
       ++direntry;

       int sector;

       direntry += 4;
       sector = (*direntry++) << 24;
       sector |= (*direntry++) << 16;
       sector |= (*direntry++) << 8;
       sector |= (*direntry++);        

       int size;

       direntry += 4;

       size = (*direntry++) << 24;
       size |= (*direntry++) << 16;
       size |= (*direntry++) << 8;
       size |= (*direntry++);

       direntry += 7; // skip date

       int flags = *direntry++;
       ++direntry; ++direntry; direntry += 4;

       int nl = *direntry++;

       char* name = &DVDToc.file[files].name[0];

       DVDToc.file[files].sector = sector;
       DVDToc.file[files].size = size;
       DVDToc.file[files].flags = flags;

       if ((nl == 1) && (direntry[0] == 1)) // ".."
       {
               DVDToc.file[files].name[0] = 0;
               if (last_current_dir != sector)
                       files++;
       }
       else if ((nl == 1) && (direntry[0] == 0))
       {
               last_current_dir = sector;
       }
       else
       {
               if (is_unicode)
               {
                       int i;
                       for (i = 0; i < (nl / 2); ++i)
                               name[i] = direntry[i * 2 + 1];
                       name[i] = 0;
                       nl = i;
               }
               else
               {
                       memcpy(name, direntry, nl);
                       name[nl] = 0;
               }

               if (!(flags & 2))
               {
                       if (name[nl - 2] == ';')
                               name[nl - 2] = 0;

                       int i = nl;
                       while (i >= 0)
                               if (name[i] == '.')
                                       break;
                               else
                                       --i;

                       ++i;

               }
               else
               {
                       name[nl++] = '/';
                       name[nl] = 0;
               }

               files++;
       }

       return nrb;
}



void read_directory(int sector, int len)
{
  int ptr = 0;
  unsigned char *sector_buffer = (unsigned char*)memalign(32,2048);
  read_sector(sector_buffer, sector);
  
  files = 0;
  memset(&DVDToc,0,sizeof(file_entries));
  while (len > 0)
  {
    ptr += read_direntry(sector_buffer + ptr);
    if (ptr >= 2048 || !sector_buffer[ptr])
    {
      len -= 2048;
      sector++;
      read_sector(sector_buffer, sector);
      ptr = 0;
    }
  }
  free(sector_buffer);
}

int dvd_read_directoryentries(uint64_t offset, int size) {
  int sector = 16;
  unsigned char *bufferDVD = (unsigned char*)memalign(32,2048);
  struct pvd_s* pvd = 0;
  struct pvd_s* svd = 0;
  
  while (sector < 32)
  {
    if (read_sector(bufferDVD, sector))
    {
      free(bufferDVD);
      return FATAL_ERROR;
    }
    if (!memcmp(((struct pvd_s *)bufferDVD)->id, "\2CD001\1", 8))
    {
      svd = (void*)bufferDVD;
      break;
    }
    ++sector;
  }
  
  
  if (!svd)
  {
    sector = 16;
    while (sector < 32)
    {
      if (read_sector(bufferDVD, sector))
      {
        free(bufferDVD);
        return FATAL_ERROR;
      }
      
      if (!memcmp(((struct pvd_s *)bufferDVD)->id, "\1CD001\1", 8))
      {
        pvd = (void*)bufferDVD;
        break;
      }
      ++sector;
    }
  }
  
  if ((!pvd) && (!svd))
  {
    free(bufferDVD);
    return NO_ISO9660_DISC;
  }
  
  files = 0;
  if (svd)
  {
    is_unicode = 1;
    read_direntry(svd->root_direntry);
  }
  else
  {
    is_unicode = 0;
    read_direntry(pvd->root_direntry);
  }
  
  if((size + offset) == 0)  // enter root
    read_directory(DVDToc.file[0].sector, DVDToc.file[0].size);
  else
    read_directory(offset>>11, size);

  free(bufferDVD);
  if(files>0)
    return files;
  return NO_FILES;
}




