/****************************************************************************
 * GC Zip Extension
 *
 * The idea here is not to support every zip file on the planet!
 * The unzip routine will simply unzip the first file in the zip archive.
 *
 * For maximum compression, I'd recommend using 7Zip,
 *	7za a -tzip -mx=9 rom.zip rom.smc
 ****************************************************************************/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "gczip.h"


int zipoffset = 0;
z_stream zs;

int inflate_init(PKZIPHEADER* pkzip){
	/*** Prepare the zip stream ***/
	zipoffset = ( sizeof(PKZIPHEADER) + FLIP16(pkzip->filenameLength) + FLIP16(pkzip->extraDataLength ));

	memset(&zs, 0, sizeof(z_stream));
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;
	zs.avail_in = 0;
	zs.next_in = Z_NULL;
	return inflateInit2(&zs, -MAX_WBITS);
}

int inflate_chunk(void* dst, void* src, int length, int uncompressedSize){
	zs.avail_in = (length  - zipoffset);
	zs.next_in = (src + zipoffset);
	int res;
	int bufferoffset = 0;

	/*** Now inflate ***/
	zs.avail_out = uncompressedSize;
	zs.next_out = dst;

	res = inflate(&zs, Z_NO_FLUSH);

	if(res == Z_MEM_ERROR) {
		inflateEnd(&zs);
		return -1;
	}

	if(res == Z_STREAM_END){
		inflateEnd(&zs);
		return 0;
	}
	
	zipoffset = 0;
	return bufferoffset;
}
