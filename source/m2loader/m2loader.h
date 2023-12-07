/**
 * M.2 Loader Driver for GameCube.
 *
 * M.2 Loader is M.2 SATA SSD adapter for Serial Port 1.
 *
 * Based on IDE-EXI Driver from Swiss
 * Based loosely on code written by Dampro
 * Re-written by emu_kidid, Extrems, webhdx
 **/

#ifndef M2LOADER_H
#define M2LOADER_H

#include <gccore.h>
#include <ogc/disc_io.h>

#define EXI_M2LOADER_ID 0x49444533
#define DEVICE_TYPE_GC_M2LOADER (('M' << 24) | ('2' << 16) | ('L' << 8) | 'R')

extern const DISC_INTERFACE __io_m2ldr;

// ATA status register bits
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DSC 0x10
#define ATA_SR_DRQ 0x08
#define ATA_SR_CORR 0x04
#define ATA_SR_IDX 0x02
#define ATA_SR_ERR 0x01

// ATA error register bits
#define ATA_ER_UNC 0x40
#define ATA_ER_MC 0x20
#define ATA_ER_IDNF 0x10
#define ATA_ER_MCR 0x08
#define ATA_ER_ABRT 0x04
#define ATA_ER_TK0NF 0x02
#define ATA_ER_AMNF 0x01

// ATA head register bits
#define ATA_HEAD_USE_LBA 0x40

// NOTE: cs0 then cs1!
// ATA registers address        val  - cs0 cs1 a2 a1 a0
#define ATA_REG_DATA 0x10          // 1 0000b
#define ATA_REG_COMMAND 0x17       // 1 0111b
#define ATA_REG_ALTSTATUS 0x0E     // 0 1110b
#define ATA_REG_DEVICE 0x16        // 1 0110b
#define ATA_REG_DEVICECONTROL 0x0E // 0 1110b
#define ATA_REG_ERROR 0x11         // 1 0001b
#define ATA_REG_FEATURES 0x11      // 1 0001b
#define ATA_REG_LBAHI 0x15         // 1 0101b
#define ATA_REG_CYLHI 0x15         // 1 0101b
#define ATA_REG_LBAMID 0x14        // 1 0100b
#define ATA_REG_CYLLO 0x14         // 1 0100b
#define ATA_REG_LBALO 0x13         // 1 0011b
#define ATA_REG_STARTSEC 0x13      // 1 0011b
#define ATA_REG_SECCOUNT 0x12      // 1 0010b
#define ATA_REG_STATUS 0x17        // 1 0111b

// ATA commands
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READSECT 0x21
#define ATA_CMD_READSECTEXT 0x24
#define ATA_CMD_WRITESECT 0x30
#define ATA_CMD_WRITESECTEXT 0x34
#define ATA_CMD_UNLOCK 0xF2
#define ATA_CMD_SECURITY_DISABLE 0xF6

// ATA Identity fields
// all offsets refer to word offset (2 byte increments)
#define ATA_IDENT_CYLINDERS 1      // number of logical cylinders
#define ATA_IDENT_HEADS 3          // number of logical heads
#define ATA_IDENT_SECTORS 6        // number of sectors per track
#define ATA_IDENT_SERIAL 10        // Drive serial (20 characters)
#define ATA_IDENT_MODEL 27         // Drive model name (40 characters)
#define ATA_IDENT_LBASECTORS 60    // Number of sectors in LBA translation mode
#define ATA_IDENT_COMMANDSET 83    // Command sets supported
#define ATA_IDENT_LBA48SECTORS 100 // Number of sectors in LBA 48-bit mode
#define ATA_IDENT_LBA48MASK 0x4    // Mask for LBA support in the command set top byte

// typedefs
// drive info structure
typedef struct
{
    u64 sizeInSectors;
    u32 sizeInGigaBytes;
    u32 cylinders;
    u32 heads;   // per cylinder
    u32 sectors; // per track
    int lba48Support;
    char model[48];
    char serial[24];
} typeDriveInfo;

typedef struct
{
    u16 type; // 1 = master pw, 0 = user
    char password[32];
    u8 reserved[478];
} unlockStruct;

extern typeDriveInfo M2LoaderDriveInfo;

// Main SDK
int M2Loader_Unlock(int useMaster, char *password, int command);
int M2Loader_ReadSectors(u64 sector, unsigned int numSectors, unsigned char *dest);
int M2Loader_WriteSectors(u64 sector, unsigned int numSectors, unsigned char *src);
int M2Loader_Shutdown();
bool M2Loader_IsInserted();
bool M2Loader_IsDriveInserted();

// Low level access functions
u8 _M2Loader_ReadStatusReg();
u8 _M2Loader_ReadErrorReg();

void _M2Loader_WriteByte(u8 addr, u8 data);

u16 _M2Loader_ReadU16();
void _M2Loader_WriteU16(u16 data);

void _M2Loader_ReadBuffer(u32 *dst);
void _M2Loader_WriteBuffer(u32 *src);

int _M2Loader_ReadSector(u64 lba, u32 *Buffer);
int _M2Loader_WriteSector(u64 lba, u32 *Buffer);

int _M2Loader_ReadSectors(u64 sector, unsigned int numSectors, unsigned char *dest);
int _M2Loader_WriteSectors(u64 sector, unsigned int numSectors, unsigned char *src);

void _M2Loader_PrintHddSector(u32 *dest);
#endif
