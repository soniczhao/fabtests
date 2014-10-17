/*
 * Copyright (c) 2013-2014 Intel Corporation.  All rights reserved.
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>

int main(int argc, char **argv)
{
	int ret = 0;
	int op;
	char *dst_addr = NULL, *src_addr = NULL;
	char *port = "9228";
	bool server = true;
	bool server_reject = false;
	int max_credits = 128;
	void *buf = NULL;
	size_t buffer_size = 128;

	struct fi_info *info = NULL;
	struct fi_info hints;
	struct fi_domain_attr domain_hints;
	struct fi_ep_attr ep_hints;
	struct fi_eq_attr cm_attr;
	struct fi_cq_attr cq_attr;
	struct fid_pep *pep;
	struct fid_fabric *fab;
	struct fid_eq *cmeq;
	struct fi_eq_cm_entry entry;
	struct fid_domain *dom;
	struct fid_ep *ep;
	struct fid_cq *rcq, *scq;
	struct fid_mr *mr;
	enum fi_eq_event event;
	struct fi_cq_entry comp;

	while ((op = getopt(argc, argv, "d:s:")) != -1) {
		switch (op) {
		case 'd':
			dst_addr = optarg;
			server = false;
			break;
		case 's':
			src_addr = optarg;
			break;
		}
	}

	memset(&hints, 0, sizeof hints);
	memset(&domain_hints, 0, sizeof domain_hints);
	memset(&ep_hints, 0, sizeof ep_hints);
	hints.domain_attr = &domain_hints;
	hints.ep_attr = &ep_hints;
	hints.type = FI_EP_MSG;
	hints.ep_cap = FI_MSG;
	hints.addr_format = FI_SOCKADDR;
	domain_hints.caps = FI_LOCAL_MR;
	domain_hints.name = "EXP";
	ep_hints.protocol = FI_PROTO_UNSPEC;

	buf = malloc(buffer_size);
	if (!buf) {
		goto exit_0;
	}

	if (server) {
		hints.ep_cap |= FI_PASSIVE;
		if ((ret = fi_getinfo(FI_VERSION(1, 0), src_addr, port, 0, &hints, &info))) {
			goto exit_0;
		}
		if ((ret = fi_fabric(info->fabric_attr, &fab, NULL))) {
			goto exit_0;
		}
		if ((ret = fi_pendpoint(fab, info, &pep, NULL))) {
			goto exit_1;
		}
		fi_freeinfo(info);
		info = NULL;
		memset(&cm_attr, 0, sizeof cm_attr);
		cm_attr.wait_obj = FI_WAIT_FD;
		if ((ret = fi_eq_open(fab, &cm_attr, &cmeq, NULL))) {
			goto exit_2;
		}
		if ((ret = fi_bind(&pep->fid, &cmeq->fid, 0))) {
			goto exit_3;
		}
		if ((ret = fi_listen(pep))) {
			goto exit_3;
		}
		if ((fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0) != sizeof entry)) {
			ret = -FI_EOTHER;
			goto exit_3;
		}
		info = entry.info;
		if (event != FI_CONNREQ) {
			ret = -FI_EOTHER;
			server_reject = true;
			goto exit_4;
		}
		if ((ret = fi_domain(fab, info->domain_attr, &dom, NULL))) {
			server_reject = true;
			goto exit_4;
		}
		if ((ret = fi_endpoint(dom, info, &ep, NULL))) {
			server_reject = true;
			goto exit_5;
		}
		memset(&cq_attr, 0, sizeof cq_attr);
		cq_attr.format = FI_CQ_FORMAT_CONTEXT;
		cq_attr.wait_obj = FI_WAIT_NONE;
		cq_attr.size = max_credits << 1;
		if ((ret = fi_cq_open(dom, &cq_attr, &scq, NULL))) {
			server_reject = true;
			goto exit_6;
		}
		if ((ret = fi_cq_open(dom, &cq_attr, &rcq, NULL))) {
			server_reject = true;
			goto exit_7;
		}
		if ((ret = fi_mr_reg(dom, buf, buffer_size, 0, 0, 0, 0, &mr, NULL))) {
			server_reject = true;
			goto exit_8;
		}
		if ((ret = fi_bind(&ep->fid, &cmeq->fid, 0))) {
			server_reject = true;
			goto exit_9;
		}
		if ((ret = fi_bind(&ep->fid, &scq->fid, FI_SEND))) {
			server_reject = true;
			goto exit_9;
		}
		if ((ret = fi_bind(&ep->fid, &rcq->fid, FI_RECV))) {
			server_reject = true;
			goto exit_9;
		}
		if ((ret = fi_enable(ep))) {
			server_reject = true;
			goto exit_9;
		}
		if ((ret = fi_accept(ep, NULL, 0))) {
			server_reject = true;
			goto exit_9;
		}
		if ((fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0) != sizeof entry)) {
			ret = -FI_EOTHER;
			server_reject = true;
			goto exit_9;
		}
		if (event != FI_COMPLETE || entry.fid != &ep->fid) {
			ret = -FI_EOTHER;
			server_reject = true;
			goto exit_9;
		}
	} else {
		if (src_addr) {
			struct addrinfo *ai;
			if (!getaddrinfo(src_addr, NULL, NULL, &ai)) {
				if ((hints.src_addr = malloc(ai->ai_addrlen))) {
					memcpy(hints.src_addr, ai->ai_addr, ai->ai_addrlen);
					hints.src_addrlen = ai->ai_addrlen;
				}
				freeaddrinfo(ai);
			}
		}
		if ((ret = fi_getinfo(FI_VERSION(1, 0), dst_addr, port, 0, &hints, &info))) {
			if (hints.src_addr) {
				free(hints.src_addr);
			}
			goto exit_0;
		}
		if (hints.src_addr) {
			free(hints.src_addr);
		}
		if ((ret = fi_fabric(info->fabric_attr, &fab, NULL))) {
			goto exit_1;
		}
		memset(&cm_attr, 0, sizeof cm_attr);
		cm_attr.wait_obj = FI_WAIT_FD;
		if ((ret = fi_eq_open(fab, &cm_attr, &cmeq, NULL))) {
			goto exit_2;
		}
		if ((ret = fi_domain(fab, info->domain_attr, &dom, NULL))) {
			goto exit_3;
		}
		if ((ret = fi_endpoint(dom, info, &ep, NULL))) {
			goto exit_5;
		}
		memset(&cq_attr, 0, sizeof cq_attr);
		cq_attr.format = FI_CQ_FORMAT_CONTEXT;
		cq_attr.wait_obj = FI_WAIT_NONE;
		cq_attr.size = max_credits << 1;
		if ((ret = fi_cq_open(dom, &cq_attr, &scq, NULL))) {
			goto exit_6;
		}
		if ((ret = fi_cq_open(dom, &cq_attr, &rcq, NULL))) {
			goto exit_7;
		}
		if ((ret = fi_mr_reg(dom, buf, buffer_size, 0, 0, 0, 0, &mr, NULL))) {
			goto exit_8;
		}
		if ((ret = fi_bind(&ep->fid, &cmeq->fid, 0))) {
			goto exit_9;
		}
		if ((ret = fi_bind(&ep->fid, &scq->fid, FI_SEND))) {
			goto exit_9;
		}
		if ((ret = fi_bind(&ep->fid, &rcq->fid, FI_RECV))) {
			goto exit_9;
		}
		if ((ret = fi_enable(ep))) {
			goto exit_9;
		}
		if ((ret = fi_connect(ep, info->dest_addr, NULL, 0))) {
			goto exit_9;
		}
		if ((fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0) != sizeof entry)) {
			ret = -FI_EOTHER;
			goto exit_9;
		}
		if (event != FI_COMPLETE || entry.fid != &ep->fid) {
			ret = -FI_EOTHER;
			goto exit_9;
		}
	}

	if (server) {
		strncpy((char*)buf, "Marry had a little lamb, his fleece was white as snow.", buffer_size);
		printf("Contents in buffer: %s\n", (char*)buf);
		printf("Receving ...\n");
		while (!ret) {
			ret = fi_cq_read(rcq, &comp, sizeof comp);
			if (ret > 0) {
				printf("Receving done.\n");
				printf("Contents in buffer: %s\n", (char*)buf);
			} else if (ret < 0) {
				goto exit_10;
			}
		};
	} else {
		strncpy((char*)buf, "He followed her to school one day, which was against the rule.", buffer_size);
		printf("Contents in buffer: %s\n", (char*)buf);
		printf("Sending ...\n");
		if ((ret = fi_send(ep, buf, buffer_size, fi_mr_desc(mr), NULL))) {
			goto exit_10;
		}
		while (!ret) {
			ret = fi_cq_read(scq, &comp, sizeof comp);
			if (ret > 0) {
				printf("Sending done.\n");
			} else if (ret < 0) {
				goto exit_10;
			}
		};
	}

exit_10:
	fi_shutdown(ep, 0);
exit_9:
	fi_close(&mr->fid);
exit_8:
	fi_close(&rcq->fid);
exit_7:
	fi_close(&scq->fid);
exit_6:
	fi_close(&ep->fid);
exit_5:
	fi_close(&dom->fid);
exit_4:
	if (server && server_reject) {
		fi_reject(pep, info->connreq, NULL, 0);
	}
exit_3:
	fi_close(&cmeq->fid);
exit_2:
	if (server) {
		fi_close(&pep->fid);
	}
exit_1:
	fi_close(&fab->fid);
exit_0:
	if (info) {
		fi_freeinfo(info);
	}
	if (buf) {
		free(buf);
	}
	return ret;
}

