/**
 * M.2 Loader Driver for GameCube.
 *
 * M.2 Loader is M.2 SATA SSD adapter for Serial Port 1.
 *
 * Based on IDE-EXI Driver from Swiss
 * Based loosely on code written by Dampro
 * Re-written by emu_kidid, Extrems, webhdx
 **/

#include <stdio.h>
#include <gccore.h> /*** Wrapper to include common libogc headers ***/
#include <ogcsys.h> /*** Needed for console support ***/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <ogc/exi.h>
#include <ogc/machine/processor.h>
#include <malloc.h>
#include "m2loader.h"

// #define _M2LDR_DEBUG

extern void usleep(int s);

extern void print_gecko(const char* fmt, ...);

u16 buffer[256] ATTRIBUTE_ALIGN(32);
static bool is_drive_inserted = false;

// Drive information struct
drive_info m2loader_drive_info;

// Returns 8 bits from the ATA Status register
inline u8 _m2loader_read_status_reg(void)
{
    // read ATA_REG_CMDSTATUS1 | 0x00 (dummy)
    u16 dat = 0x1700;
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 2, EXI_WRITE);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 1, EXI_READ);
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);

    return *(u8 *)&dat;
}

// Returns 8 bits from the ATA Error register
inline u8 _m2loader_read_error_reg(void)
{
    // read ATA_REG_ERROR | 0x00 (dummy)
    u16 dat = 0x1100;
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 2, EXI_WRITE);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 1, EXI_READ);
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);

    return *(u8 *)&dat;
}

// Writes 8 bits of data out to the specified ATA Register
inline void _m2loader_write_byte(u8 addr, u8 data)
{
    u32 dat = 0x80000000 | (addr << 24) | (data << 16);
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 3, EXI_WRITE);
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);
}

// Writes 16 bits to the ATA Data register
inline void _m2loader_write_u16(u16 data)
{
    // write 16 bit to ATA_REG_DATA | data LSB | data MSB | 0x00 (dummy)
    u32 dat = 0xD0000000 | (((data >> 8) & 0xff) << 16) | ((data & 0xff) << 8);
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 4, EXI_WRITE);
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);
}

// Returns 16 bits from the ATA Data register
inline u16 _m2loader_read_u16(void)
{
    // read 16 bit from ATA_REG_DATA | 0x00 (dummy)
    u16 dat = 0x5000;
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 2, EXI_WRITE);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 2, EXI_READ); // read LSB & MSB
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);

    return dat;
}

// Reads 512 bytes
inline void _m2loader_read_buffer(u32 *dst)
{
    u16 dwords = 128; // 128 * 4 = 512 bytes
    // (31:29) 011b | (28:24) 10000b | (23:16) <num_words_LSB> | (15:8) <num_words_MSB> | (7:0) 00h (4 bytes)
    u32 dat = 0x70000000 | ((dwords & 0xff) << 16) | (((dwords >> 8) & 0xff) << 8);
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 4, EXI_WRITE);

    u32 *ptr = dst;
    if (((u32)dst) % 32)
    {
        ptr = (u32 *)memalign(32, 512);
    }

    DCInvalidateRange(ptr, 512);
    EXI_Dma(EXI_CHANNEL_0, ptr, 512, EXI_READ, NULL);
    EXI_Sync(EXI_CHANNEL_0);
    if (((u32)dst) % 32)
    {
        memcpy(dst, ptr, 512);
        free(ptr);
    }

    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);
}

inline void _m2loader_write_buffer(u32 *src)
{
    u16 dwords = 128; // 128 * 4 = 512 bytes
    // (23:21) 111b | (20:16) 10000b | (15:8) <num_words_LSB> | (7:0) <num_words_MSB> (3 bytes)
    u32 dat = 0xF0000000 | ((dwords & 0xff) << 16) | (((dwords >> 8) & 0xff) << 8);
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_2, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_2, EXI_SPEED32MHZ);
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 3, EXI_WRITE);
    EXI_ImmEx(EXI_CHANNEL_0, src, 512, EXI_WRITE);
    dat = 0;
    EXI_ImmEx(EXI_CHANNEL_0, &dat, 1, EXI_WRITE); // Burn an extra cycle for the M.2 Loader to know to stop serving data
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);
}

void _m2loader_print_hdd_sector(u32 *dest)
{
    int i = 0;
    for (i = 0; i < 512 / 4; i += 4)
    {
        print_gecko("%08X:%08X %08X %08X %08X\r\n", i * 4, dest[i], dest[i + 1], dest[i + 2], dest[i + 3]);
    }
}

bool m2loader_is_inserted(void)
{
    u32 cid = 0;
    EXI_GetID(EXI_CHANNEL_0, EXI_DEVICE_2, &cid);

    return cid == EXI_M2LOADER_ID;
}

// Sends the IDENTIFY command to the SSD
// Returns 0 on success, -1 otherwise
u32 _m2loader_drive_identify()
{
    u16 tmp, retries = 50;
    u32 i = 0;

    memset(&m2loader_drive_info, 0, sizeof(drive_info));

    // Select the device
    _m2loader_write_byte(ATA_REG_DEVICE, 0 /*ATA_HEAD_USE_LBA*/);

    // Wait for drive to be ready (BSY to clear) - 5 sec timeout
    do
    {
        tmp = _m2loader_read_status_reg();
        usleep(100000); // sleep for 0.1 seconds
        retries--;
#ifdef _M2LDR_DEBUG
        print_gecko("(%08X) Waiting for BSY to clear..\r\n", tmp);
#endif
    } while ((tmp & ATA_SR_BSY) && retries);
    if (!retries)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("Exceeded retries..\r\n");
#endif

        return -1;
    }

    // Write the identify command
    _m2loader_write_byte(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    // Wait for drive to request data transfer - 1 sec timeout
    retries = 10;
    do
    {
        tmp = _m2loader_read_status_reg();
        usleep(100000); // sleep for 0.1 seconds
        retries--;
#ifdef _M2LDR_DEBUG
        print_gecko("(%08X) Waiting for DRQ to toggle..\r\n", tmp);
#endif
    } while ((!(tmp & ATA_SR_DRQ)) && retries);
    if (!retries)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("(%08X) Drive did not respond in time, failing M.2 Loader init..\r\n", tmp);
#endif

        return -1;
    }
    usleep(2000);

    u16 *ptr = (u16 *)(&buffer[0]);

    // Read Identify data from drive
    for (i = 0; i < 256; i++)
    {
        tmp = _m2loader_read_u16(); // get data
        *ptr++ = bswap16(tmp);     // swap
    }

    // Get the info out of the Identify data buffer
    // From the command set, check if LBA48 is supported
    u16 command_set = *(u16 *)(&buffer[ATA_IDENT_COMMANDSET]);
    m2loader_drive_info.lba48_support = (command_set >> 8) & ATA_IDENT_LBA48MASK;

    if (m2loader_drive_info.lba48_support)
    {
        u16 lbaHi = *(u16 *)(&buffer[ATA_IDENT_LBA48SECTORS + 2]);
        u16 lbaMid = *(u16 *)(&buffer[ATA_IDENT_LBA48SECTORS + 1]);
        u16 lbaLo = *(u16 *)(&buffer[ATA_IDENT_LBA48SECTORS]);
        m2loader_drive_info.size_in_sectors = (u64)(((u64)lbaHi << 32) | (lbaMid << 16) | lbaLo);
        m2loader_drive_info.size_in_gigabytes = (u32)((m2loader_drive_info.size_in_sectors << 9) / 1024 / 1024 / 1024);
    }
    else
    {
        m2loader_drive_info.cylinders = *(u16 *)(&buffer[ATA_IDENT_CYLINDERS]);
        m2loader_drive_info.heads = *(u16 *)(&buffer[ATA_IDENT_HEADS]);
        m2loader_drive_info.sectors = *(u16 *)(&buffer[ATA_IDENT_SECTORS]);
        m2loader_drive_info.size_in_sectors = ((*(u16 *)&buffer[ATA_IDENT_LBASECTORS + 1]) << 16) |
                                          (*(u16 *)&buffer[ATA_IDENT_LBASECTORS]);
        m2loader_drive_info.size_in_gigabytes = (u32)((m2loader_drive_info.size_in_sectors << 9) / 1024 / 1024 / 1024);
    }

    i = 20;
    // copy serial string
    memcpy(&m2loader_drive_info.serial[0], &buffer[ATA_IDENT_SERIAL], 20);
    // cut off the string (usually has trailing spaces)
    while ((m2loader_drive_info.serial[i] == ' ' || !m2loader_drive_info.serial[i]) && i >= 0)
    {
        m2loader_drive_info.serial[i] = 0;
        i--;
    }
    // copy model string
    memcpy(&m2loader_drive_info.model[0], &buffer[ATA_IDENT_MODEL], 40);
    // cut off the string (usually has trailing spaces)
    i = 40;
    while ((m2loader_drive_info.model[i] == ' ' || !m2loader_drive_info.model[i]) && i >= 0)
    {
        m2loader_drive_info.model[i] = 0;
        i--;
    }

#ifdef _M2LDR_DEBUG
    print_gecko("%d GB SDD Connected\r\n", m2loader_drive_info.size_in_gigabytes);
    print_gecko("LBA 48-Bit Mode %s\r\n", m2loader_drive_info.lba48_support ? "Supported" : "Not Supported");
    if (!m2loader_drive_info.lba48_support)
    {
        print_gecko("Cylinders: %i\r\n", m2loader_drive_info.cylinders);
        print_gecko("Heads Per Cylinder: %i\r\n", m2loader_drive_info.heads);
        print_gecko("Sectors Per Track: %i\r\n", m2loader_drive_info.sectors);
    }
    print_gecko("Model: %s\r\n", m2loader_drive_info.model);
    print_gecko("Serial: %s\r\n", m2loader_drive_info.serial);
    _m2loader_print_hdd_sector((u32 *)&buffer);
#endif

    return 0;
}

// Unlocks a ATA HDD with a password
// Returns 0 on success, -1 on failure.
int m2loader_unlock(int useMaster, char *password, int command)
{
    u32 i;
    u16 tmp, retries = 50;

    // Select the device
    _m2loader_write_byte(ATA_REG_DEVICE, ATA_HEAD_USE_LBA);

    // Wait for drive to be ready (BSY to clear) - 5 sec timeout
    do
    {
        tmp = _m2loader_read_status_reg();
        usleep(100000); // sleep for 0.1 seconds
        retries--;
#ifdef _M2LDR_DEBUG
        print_gecko("UNLOCK (%08X) Waiting for BSY to clear..\r\n", tmp);
#endif
    } while ((tmp & ATA_SR_BSY) && retries);
    if (!retries)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("UNLOCK Exceeded retries..\r\n");
#endif

        return -1;
    }

    // Write the appropriate unlock command
    _m2loader_write_byte(ATA_REG_COMMAND, command);

    // Wait for drive to request data transfer - 1 sec timeout
    retries = 10;
    do
    {
        tmp = _m2loader_read_status_reg();
        usleep(100000); // sleep for 0.1 seconds
        retries--;
#ifdef _M2LDR_DEBUG
        print_gecko("UNLOCK (%08X) Waiting for DRQ to toggle..\r\n", tmp);
#endif
    } while ((!(tmp & ATA_SR_DRQ)) && retries);
    if (!retries)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("UNLOCK (%08X) Drive did not respond in time, failing M.2 Loader init..\r\n", tmp);
#endif

        return -1;
    }
    usleep(2000);

    // Fill an unlock struct
    unlock_struct unlock;
    memset(&unlock, 0, sizeof(unlock_struct));
    unlock.type = (u16)useMaster;
    memcpy(unlock.password, password, strlen(password));

    // write data to the drive
    u16 *ptr = (u16 *)&unlock;
    for (i = 0; i < 256; i++)
    {
        ptr[i] = bswap16(ptr[i]);
        _m2loader_write_u16(ptr[i]);
    }

    // Wait for BSY to clear
    u32 temp = 0;
    while ((temp = _m2loader_read_status_reg()) & ATA_SR_BSY)
        ;

    // If the error bit was set, fail.
    if (temp & ATA_SR_ERR)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("Error: %02X\r\n", _m2loader_read_error_reg());
#endif

        return 1;
    }

    return !(_m2loader_read_error_reg() & ATA_ER_ABRT);
}

// Reads sectors from the specified lba, for the specified slot
// Returns 0 on success, -1 on failure.
int _m2loader_read_sector(u64 lba, u32 *Buffer)
{
    u32 temp = 0;

    // Wait for drive to be ready (BSY to clear)
    while (_m2loader_read_status_reg() & ATA_SR_BSY)
        ;

    // Select the device differently based on 28 or 48bit mode
    if (m2loader_drive_info.lba48_support)
    {
        // Select the device (ATA_HEAD_USE_LBA is 0x40 for master, 0x50 for slave)
        _m2loader_write_byte(ATA_REG_DEVICE, ATA_HEAD_USE_LBA);
    }
    else
    {
        // Select the device (ATA_HEAD_USE_LBA is 0x40 for master, 0x50 for slave)
        _m2loader_write_byte(ATA_REG_DEVICE, 0xE0 | (u8)((lba >> 24) & 0x0F));
    }

    // check if drive supports LBA 48-bit
    if (m2loader_drive_info.lba48_support)
    {
        _m2loader_write_byte(ATA_REG_SECCOUNT, 0);                      // Sector count (Hi)
        _m2loader_write_byte(ATA_REG_LBALO, (u8)((lba >> 24) & 0xFF));  // LBA 4
        _m2loader_write_byte(ATA_REG_LBAMID, (u8)((lba >> 32) & 0xFF)); // LBA 5
        _m2loader_write_byte(ATA_REG_LBAHI, (u8)((lba >> 40) & 0xFF));  // LBA 6
        _m2loader_write_byte(ATA_REG_SECCOUNT, 1);                      // Sector count (Lo)
        _m2loader_write_byte(ATA_REG_LBALO, (u8)(lba & 0xFF));          // LBA 1
        _m2loader_write_byte(ATA_REG_LBAMID, (u8)((lba >> 8) & 0xFF));  // LBA 2
        _m2loader_write_byte(ATA_REG_LBAHI, (u8)((lba >> 16) & 0xFF));  // LBA 3
    }
    else
    {
        _m2loader_write_byte(ATA_REG_SECCOUNT, 1);                     // Sector count
        _m2loader_write_byte(ATA_REG_LBALO, (u8)(lba & 0xFF));         // LBA Lo
        _m2loader_write_byte(ATA_REG_LBAMID, (u8)((lba >> 8) & 0xFF)); // LBA Mid
        _m2loader_write_byte(ATA_REG_LBAHI, (u8)((lba >> 16) & 0xFF)); // LBA Hi
    }

    // Write the appropriate read command
    _m2loader_write_byte(ATA_REG_COMMAND, m2loader_drive_info.lba48_support ? ATA_CMD_READSECTEXT : ATA_CMD_READSECT);

    // Wait for BSY to clear
    while ((temp = _m2loader_read_status_reg()) & ATA_SR_BSY)
        ;

    // If the error bit was set, fail.
    if (temp & ATA_SR_ERR)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("Error: %02X", _m2loader_read_error_reg());
#endif

        return 1;
    }

    // Wait for drive to request data transfer
    while (!(_m2loader_read_status_reg() & ATA_SR_DRQ))
        ;

    // Read data from drive
    _m2loader_read_buffer(Buffer);

    temp = _m2loader_read_status_reg();
    if (temp & ATA_SR_ERR)
    {
        return 1; // If the error bit was set, fail.
    }

    return temp & ATA_SR_ERR;
}

// Writes sectors to the specified lba, for the specified slot
// Returns 0 on success, -1 on failure.
int _m2loader_write_sector(u64 lba, u32 *Buffer)
{
    u32 temp;

    // Wait for drive to be ready (BSY to clear)
    while (_m2loader_read_status_reg() & ATA_SR_BSY)
        ;

    // Select the device differently based on 28 or 48bit mode
    if (m2loader_drive_info.lba48_support)
    {
        // Select the device (ATA_HEAD_USE_LBA is 0x40 for master, 0x50 for slave)
        _m2loader_write_byte(ATA_REG_DEVICE, ATA_HEAD_USE_LBA);
    }
    else
    {
        // Select the device (ATA_HEAD_USE_LBA is 0x40 for master, 0x50 for slave)
        _m2loader_write_byte(ATA_REG_DEVICE, 0xE0 | (u8)((lba >> 24) & 0x0F));
    }

    // check if drive supports LBA 48-bit
    if (m2loader_drive_info.lba48_support)
    {
        _m2loader_write_byte(ATA_REG_SECCOUNT, 0);                      // Sector count (Hi)
        _m2loader_write_byte(ATA_REG_LBALO, (u8)((lba >> 24) & 0xFF));  // LBA 4
        _m2loader_write_byte(ATA_REG_LBAMID, (u8)((lba >> 32) & 0xFF)); // LBA 4
        _m2loader_write_byte(ATA_REG_LBAHI, (u8)((lba >> 40) & 0xFF));  // LBA 5
        _m2loader_write_byte(ATA_REG_SECCOUNT, 1);                      // Sector count (Lo)
        _m2loader_write_byte(ATA_REG_LBALO, (u8)(lba & 0xFF));          // LBA 1
        _m2loader_write_byte(ATA_REG_LBAMID, (u8)((lba >> 8) & 0xFF));  // LBA 2
        _m2loader_write_byte(ATA_REG_LBAHI, (u8)((lba >> 16) & 0xFF));  // LBA 3
    }
    else
    {
        _m2loader_write_byte(ATA_REG_SECCOUNT, 1);                     // Sector count
        _m2loader_write_byte(ATA_REG_LBALO, (u8)(lba & 0xFF));         // LBA Lo
        _m2loader_write_byte(ATA_REG_LBAMID, (u8)((lba >> 8) & 0xFF)); // LBA Mid
        _m2loader_write_byte(ATA_REG_LBAHI, (u8)((lba >> 16) & 0xFF)); // LBA Hi
    }

    // Write the appropriate write command
    _m2loader_write_byte(ATA_REG_COMMAND, m2loader_drive_info.lba48_support ? ATA_CMD_WRITESECTEXT : ATA_CMD_WRITESECT);

    // Wait for BSY to clear
    while ((temp = _m2loader_read_status_reg()) & ATA_SR_BSY)
        ;

    // If the error bit was set, fail.
    if (temp & ATA_SR_ERR)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("Error: %02X", _m2loader_read_error_reg());
#endif

        return 1;
    }
    // Wait for drive to request data transfer
    while (!(_m2loader_read_status_reg() & ATA_SR_DRQ))
        ;

    // Write data to the drive
    _m2loader_write_buffer(Buffer);

    // Wait for the write to finish
    while (_m2loader_read_status_reg() & ATA_SR_BSY)
        ;

    temp = _m2loader_read_status_reg();
    if (temp & ATA_SR_ERR)
    {
        return 1; // If the error bit was set, fail.
    }

    return temp & ATA_SR_ERR;
}

// Wrapper to read a number of sectors
// 0 on Success, -1 on Error
int _m2loader_read_sectors(u64 sector, unsigned int num_sectors, unsigned char *dest)
{
    int ret = 0;
    while (num_sectors)
    {
#ifdef _M2LDR_DEBUG
        print_gecko("Reading, sec %08X, num_sectors %i, dest %08X ..\r\n", (u32)(sector & 0xFFFFFFFF), num_sectors, (u32)dest);
#endif

        if ((ret = _m2loader_read_sector(sector, (u32 *)dest)))
        {
#ifdef _M2LDR_DEBUG
            print_gecko("(%08X) Failed to read!..\r\n", ret);
#endif

            return -1;
        }

#ifdef _M2LDR_DEBUG
        _m2loader_print_hdd_sector((u32 *)dest);
#endif

        dest += 512;
        sector++;
        num_sectors--;
    }

    return 0;
}

// Wrapper to write a number of sectors
// 0 on Success, -1 on Error
int _m2loader_write_sectors(u64 sector, unsigned int num_sectors, unsigned char *src)
{
    int ret = 0;
    while (num_sectors)
    {
        if ((ret = _m2loader_write_sector(sector, (u32 *)src)))
        {
#ifdef _M2LDR_DEBUG
            print_gecko("(%08X) Failed to write!..\r\n", ret);
#endif

            return -1;
        }

        src += 512;
        sector++;
        num_sectors--;
    }

    return 0;
}

bool m2loader_is_drive_inserted(void) 
{
    if (is_drive_inserted)
    {
        return true;
    }

    if (_m2loader_drive_identify())
    {
        return false;
    }

    is_drive_inserted = true;

    return true;
}

int m2loader_shutdown(void)
{
    is_drive_inserted = 0;

    return 1;
}

static bool __m2ldr_startup(void)
{
    return m2loader_is_drive_inserted();
}

static bool __m2ldr_isInserted(void)
{
    return m2loader_is_inserted() && m2loader_is_drive_inserted();
}

static bool __m2ldr_readSectors(sec_t sector, sec_t num_sectors, void *buffer)
{
    return !_m2loader_read_sectors((u64)sector, num_sectors, buffer);
}

static bool __m2ldr_writeSectors(sec_t sector, sec_t num_sectors, void *buffer)
{
    return !_m2loader_write_sectors((u64)sector, num_sectors, buffer);
}

static bool __m2ldr_clearStatus(void)
{
    return true;
}

static bool __m2ldr_shutdown(void)
{
    return true;
}

const DISC_INTERFACE __io_m2ldr = {
    DEVICE_TYPE_GC_M2LOADER,
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_GAMECUBE_PORT2,
    (FN_MEDIUM_STARTUP)&__m2ldr_startup,
    (FN_MEDIUM_ISINSERTED)&__m2ldr_isInserted,
    (FN_MEDIUM_READSECTORS)&__m2ldr_readSectors,
    (FN_MEDIUM_WRITESECTORS)&__m2ldr_writeSectors,
    (FN_MEDIUM_CLEARSTATUS)&__m2ldr_clearStatus,
    (FN_MEDIUM_SHUTDOWN)&__m2ldr_shutdown};
    