#ifndef __GCZIP_H__
#define __GCZIP_H__

/*** PKWare Zip Header ***/
#define CRC_SEED 0xFFFFFFFF
#define PKZIPID 0x504b0304
#define ZIPCHUNK 2048

typedef struct {
    unsigned int zipid __attribute__ ((__packed__));       // 0x04034b50
    unsigned short zipversion __attribute__ ((__packed__));
    unsigned short zipflags __attribute__ ((__packed__));
    unsigned short compressionMethod __attribute__ ((__packed__));
    unsigned short lastmodtime __attribute__ ((__packed__));
    unsigned short lastmoddate __attribute__ ((__packed__));
    unsigned int crc32 __attribute__ ((__packed__));
    unsigned int compressedSize __attribute__ ((__packed__));
    unsigned int uncompressedSize __attribute__ ((__packed__));
    unsigned short filenameLength __attribute__ ((__packed__));
    unsigned short extraDataLength __attribute__ ((__packed__));
} PKZIPHEADER;

static inline u32 FLIP32(u32 b){
    unsigned int c;

    c = ( b & 0xff000000 ) >> 24;
    c |= ( b & 0xff0000 ) >> 8;
    c |= ( b & 0xff00 ) << 8;
    c |= ( b & 0xff ) << 24;

    return c;
}

static inline u16 FLIP16(u16 b){
    u16 c;

    c = ( b & 0xff00 ) >> 8;
    c |= ( b &0xff ) << 8;

    return c;
}

int inflate_init(PKZIPHEADER* pkzip);

int inflate_chunk(void* dst, void* src, int length, int uncompressed);
#endif
