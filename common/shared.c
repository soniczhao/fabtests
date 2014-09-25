/*
 * Copyright (c) 2013,2014 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014 NetApp, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 * 
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <rdma/fi_errno.h>

#include "shared.h"

struct test_size_param test_size[] = {
	{ 1 <<  1, 1 }, { (1 <<  1) + (1 <<  0), 2},
	{ 1 <<  2, 1 }, { (1 <<  2) + (1 <<  1), 2},
	{ 1 <<  3, 1 }, { (1 <<  3) + (1 <<  2), 2},
	{ 1 <<  4, 1 }, { (1 <<  4) + (1 <<  3), 2},
	{ 1 <<  5, 1 }, { (1 <<  5) + (1 <<  4), 2},
	{ 1 <<  6, 1 }, { (1 <<  6) + (1 <<  5), 2},
	{ 1 <<  7, 1 }, { (1 <<  7) + (1 <<  6), 2},
	{ 1 <<  8, 1 }, { (1 <<  8) + (1 <<  7), 2},
	{ 1 <<  9, 1 }, { (1 <<  9) + (1 <<  8), 2},
	{ 1 << 10, 1 }, { (1 << 10) + (1 <<  9), 2},
	{ 1 << 11, 1 }, { (1 << 11) + (1 << 10), 2},
	{ 1 << 12, 1 }, { (1 << 12) + (1 << 11), 2},
	{ 1 << 13, 1 }, { (1 << 13) + (1 << 12), 2},
	{ 1 << 14, 1 }, { (1 << 14) + (1 << 13), 2},
	{ 1 << 15, 1 }, { (1 << 15) + (1 << 14), 2},
	{ 1 << 16, 1 }, { (1 << 16) + (1 << 15), 2},
	{ 1 << 17, 1 }, { (1 << 17) + (1 << 16), 2},
	{ 1 << 18, 1 }, { (1 << 18) + (1 << 17), 2},
	{ 1 << 19, 1 }, { (1 << 19) + (1 << 18), 2},
	{ 1 << 20, 1 }, { (1 << 20) + (1 << 19), 2},
	{ 1 << 21, 1 }, { (1 << 21) + (1 << 20), 2},
	{ 1 << 22, 1 }, { (1 << 22) + (1 << 21), 2},
	{ 1 << 23, 1 },
};

const unsigned int test_cnt = (sizeof test_size / sizeof test_size[0]);

int getaddr(char *node, char *service, struct sockaddr **addr, socklen_t *len)
{
	struct addrinfo *ai;
	int ret;

	ret = getaddrinfo(node, service, NULL, &ai);
	if (ret)
		return ret;

	if ((*addr = malloc(ai->ai_addrlen))) {
		memcpy(*addr, ai->ai_addr, ai->ai_addrlen);
		*len = ai->ai_addrlen;
	} else {
		ret = EAI_MEMORY;
	}

	freeaddrinfo(ai);
	return ret;
}

void size_str(char *str, size_t ssize, long long size)
{
	long long base, fraction = 0;
	char mag;

	if (size >= (1 << 30)) {
		base = 1 << 30;
		mag = 'g';
	} else if (size >= (1 << 20)) {
		base = 1 << 20;
		mag = 'm';
	} else if (size >= (1 << 10)) {
		base = 1 << 10;
		mag = 'k';
	} else {
		base = 1;
		mag = '\0';
	}

	if (size / base < 10)
		fraction = (size % base) * 10 / base;
	if (fraction) {
		snprintf(str, ssize, "%lld.%lld%c", size / base, fraction, mag);
	} else {
		snprintf(str, ssize, "%lld%c", size / base, mag);
	}
}

void cnt_str(char *str, size_t ssize, long long cnt)
{
	if (cnt >= 1000000000)
		snprintf(str, ssize, "%lldb", cnt / 1000000000);
	else if (cnt >= 1000000)
		snprintf(str, ssize, "%lldm", cnt / 1000000);
	else if (cnt >= 1000)
		snprintf(str, ssize, "%lldk", cnt / 1000);
	else
		snprintf(str, ssize, "%lld", cnt);
}

int size_to_count(int size)
{
	if (size >= (1 << 20))
		return 100;
	else if (size >= (1 << 16))
		return 1000;
	else if (size >= (1 << 10))
		return 10000;
	else
		return 100000;
}

int bind_fid( fid_t ep, fid_t res, uint64_t flags)
{
	int ret;

	ret = fi_bind(ep, res, flags);
	if (ret)
		printf("fi_bind %s\n", fi_strerror(-ret));
	return ret;
}

