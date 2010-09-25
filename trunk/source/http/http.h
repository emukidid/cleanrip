/****************************************************************************
 * Visual Boy Advance GX
 *
 * Tantric December 2008
 *
 * http.h
 *
 * HTTP operations
 ***************************************************************************/

#ifndef _HTTP_H_
#define _HTTP_H_

typedef enum {
	HTTPR_OK,
	HTTPR_ERR_CONNECT,
	HTTPR_ERR_REQUEST,
	HTTPR_ERR_STATUS,
	HTTPR_ERR_TOOBIG,
	HTTPR_ERR_RECEIVE
} http_res;

int http_request(char *http_host, char *http_path, u8 *buffer, u32 maxsize, bool silent, int retry);

#endif
