/****************************************************************************
 * Visual Boy Advance GX
 *
 * Tantric December 2008
 *
 * http.cpp
 *
 * HTTP operations
 * Written by dhewg/bushing, modified by Tantric
 ***************************************************************************/

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ogcsys.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <fcntl.h>

#include "FrameBufferMagic.h"
#include "http.h"

#define TCP_CONNECT_TIMEOUT 	4000  // 4 secs to make a connection
#define TCP_SEND_SIZE 			(32 * 1024)
#define TCP_RECV_SIZE 			(32 * 1024)
#define TCP_BLOCK_RECV_TIMEOUT 	4000 // 4 secs to receive
#define TCP_BLOCK_SEND_TIMEOUT 	4000 // 4 secs to send
#define TCP_BLOCK_SIZE 			1024
#define HTTP_TIMEOUT 			10000 // 10 secs to get an http response
#define IOS_O_NONBLOCK			0x04

static s32 tcp_socket(void)
{
	s32 s, res;

	s = net_socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (s < 0)
		return s;

	// Switch off Nagle with TCP_NODELAY
	u32 nodelay = 1;
	net_setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay));

	res = net_fcntl(s, F_GETFL, 0);
	if (res < 0)
	{
		net_close(s);
		return res;
	}

	res = net_fcntl(s, F_SETFL, res | IOS_O_NONBLOCK);
	if (res < 0)
	{
		net_close(s);
		return res;
	}

	return s;
}

static s32 tcp_connect(char *host, const u16 port)
{
	struct hostent *hp;
	struct sockaddr_in sa;
	fd_set myset;
	struct timeval tv;
	s32 s, res;

	s = tcp_socket();
	if (s < 0)
		return s;

	memset(&sa, 0, sizeof(struct sockaddr_in));
	sa.sin_family= PF_INET;
	sa.sin_len = sizeof(struct sockaddr_in);
	sa.sin_port= htons(port);

#ifdef HW_RVL
	hp = net_gethostbyname (host);
	if (!hp || !(hp->h_addrtype == PF_INET)) {
		return errno;
	}
	memcpy((char *) &sa.sin_addr, hp->h_addr_list[0], hp->h_length);
#endif

#ifdef HW_DOL
	u32 addr = inet_addr("205.205.224.54");
	memcpy(&sa.sin_addr, (struct in_addr*)&addr, sizeof(struct in_addr));
#endif
	res = net_connect (s, (struct sockaddr *) &sa, sizeof (sa));

	if (res == EINPROGRESS)
	{
		tv.tv_sec = TCP_CONNECT_TIMEOUT;
		tv.tv_usec = 0;
		FD_ZERO(&myset);
		FD_SET(s, &myset);
		if (net_select(s+1, NULL, &myset, NULL, &tv) <= 0)
			return -1;
	}
	return s;
}

static int tcp_readln(const s32 s, char *buf, const u16 max_length)
{
	s32 res = -1;
	s32 ret;
	u64 start_time = gettime();
	u16 c = 0;

	while (c < max_length)
	{
		if (ticks_to_millisecs(diff_ticks(start_time, gettime())) > HTTP_TIMEOUT)
			break;

		ret = net_read(s, &buf[c], 1);

		if (ret == 0 || ret == -EAGAIN)
		{
			usleep(20 * 1000);
			continue;
		}

		if (ret < 0)
			break;

		if (c > 0 && buf[c - 1] == '\r' && buf[c] == '\n')
		{
			res = 0;
			buf[c-1] = 0;
			break;
		}
		c++;
		usleep(100);
	}
	return res;
}

static int tcp_read(const s32 s, u8 *buffer, const u32 length)
{
	char *p;
	u32 left, block, received, step=0;
	s64 t;
	s32 res;

	p = (char *)buffer;
	left = length;
	received = 0;

	t = gettime();
	while (left)
	{
		if (ticks_to_millisecs(diff_ticks(t, gettime()))
				> TCP_BLOCK_RECV_TIMEOUT)
		{
			break;
		}

		block = left;
		if (block > TCP_RECV_SIZE)
			block = TCP_RECV_SIZE;

		res = net_read(s, p, block);

		if(res>0)
		{
			received += res;
			left -= res;
			p += res;
		}
		else if (res < 0 && res != -EAGAIN)
		{
			break;
		}

		usleep(1000);

		if ((received / TCP_BLOCK_SIZE) > step)
		{
			t = gettime ();
			step++;
		}
	}
	return received;
}

static int tcp_write(const s32 s, const u8 *buffer, const u32 length)
{
	const u8 *p;
	u32 left, block, sent, step=0;
	s64 t;
	s32 res;

	p = buffer;
	left = length;
	sent = 0;

	t = gettime();
	while (left)
	{
		if (ticks_to_millisecs(diff_ticks(t, gettime()))
				> TCP_BLOCK_SEND_TIMEOUT)
		{
			break;
		}

		block = left;
		if (block > TCP_SEND_SIZE)
			block = TCP_SEND_SIZE;

		res = net_write(s, p, block);

		if ((res == 0) || (res == -56))
		{
			usleep(20 * 1000);
			continue;
		}

		if (res < 0)
			break;

		sent += res;
		left -= res;
		p += res;
		usleep(100);

		if ((sent / TCP_BLOCK_SIZE) > step)
		{
			t = gettime ();
			step++;
		}
	}

	return left == 0;
}

#define MAX_SIZE (1024*1024*10)
char redirect[1024];
/****************************************************************************
 * http_request
 * Retrieves the specified URL, and stores it in the specified file or buffer
 ***************************************************************************/
int http_request(char *http_host, char *http_path, u8 *buffer, u32 maxsize, bool silent, int retry)
{
	u16 http_port;

	u32 http_status = 404;
	u32 sizeread = 0, content_length = 0;

	int linecount;

	if(maxsize > MAX_SIZE)
		return -1;

	if (http_host == NULL || http_path == NULL || (buffer == NULL))
		return -2;

	http_port = 80;

	int s = tcp_connect(http_host, http_port);

	if (s < 0) {
		return s;
	}

	char request[1024];
	char *r = request;

	r += sprintf(r, "GET %s HTTP/1.1\r\n", http_path);
	r += sprintf(r, "Host: %s\r\n", http_host);
	r += sprintf(r, "Cache-Control: no-cache\r\n\r\n");

	tcp_write(s, (u8 *) request, strlen(request));

	char line[256];
	memset(&redirect[0], 0, 1024);
	
	for (linecount = 0; linecount < 32; linecount++)
	{
		if (tcp_readln(s, line, 255) != 0)
		{
			http_status = 404;
			break;
		}

		if (strlen(line) < 1)
			break;

		sscanf(line, "HTTP/1.%*u %lu", &http_status);
		sscanf(line, "Content-Length: %lu", &content_length);
		sscanf(line, "Location: %s", redirect);
	}

	if (http_status != 200)
	{
		net_close(s);
		if((http_status == 301 || http_status == 302) && redirect[0] != 0 && retry < 5) {
			DrawMessageBox(D_INFO, "Checking for updates\nRedirect!\nDownloading...");
			sleep(5);
			return http_request(&redirect[0], &redirect[10], buffer, maxsize, silent, ++retry);
		}
		return -5;
	}

	// length unknown - just read as much as we can
	if(content_length == 0)
	{
		content_length = maxsize;
	}
	else if (content_length > maxsize)
	{
		net_close(s);
		return -6;
	}

	if (buffer != NULL)
	{
		char txtbuf[256];
		sprintf(txtbuf, "Connected to gc-forever.com\nDownloading %lu bytes", content_length);
		DrawMessageBox(D_INFO, txtbuf);
		sizeread = tcp_read(s, buffer, content_length);
	}

	net_close(s);

	if (content_length < maxsize && sizeread != content_length)
	{
		return HTTPR_ERR_RECEIVE;
	}

	return sizeread;
}

