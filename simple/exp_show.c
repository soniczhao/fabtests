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
	int max_credits = 128;
	size_t buffer_size = 128;
	void *buf = malloc(buffer_size);

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
	memset(&cm_attr, 0, sizeof cm_attr);
	memset(&cq_attr, 0, sizeof cq_attr);

	hints.domain_attr = &domain_hints;
	hints.ep_attr = &ep_hints;
	hints.type = FI_EP_MSG;
	hints.ep_cap = FI_MSG;
	hints.addr_format = FI_SOCKADDR;
	domain_hints.caps = FI_LOCAL_MR;
	domain_hints.name = "EXP";
	ep_hints.protocol = FI_PROTO_UNSPEC;
	cm_attr.wait_obj = FI_WAIT_FD;
	cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	cq_attr.wait_obj = FI_WAIT_NONE;
	cq_attr.size = max_credits << 1;

	if (server) {
		hints.ep_cap |= FI_PASSIVE;
		fi_getinfo(FI_VERSION(1, 0), src_addr, port, 0, &hints, &info);
		fi_fabric(info->fabric_attr, &fab, NULL);
		fi_pendpoint(fab, info, &pep, NULL);
		fi_freeinfo(info);
		fi_eq_open(fab, &cm_attr, &cmeq, NULL);
		fi_bind(&pep->fid, &cmeq->fid, 0);
		fi_listen(pep);

		fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0);
		info = entry.info;
		fi_domain(fab, info->domain_attr, &dom, NULL);
		fi_endpoint(dom, info, &ep, NULL);
		fi_cq_open(dom, &cq_attr, &scq, NULL);
		fi_cq_open(dom, &cq_attr, &rcq, NULL);
		fi_mr_reg(dom, buf, buffer_size, 0, 0, 0, 0, &mr, NULL);
		fi_bind(&ep->fid, &cmeq->fid, 0);
		fi_bind(&ep->fid, &scq->fid, FI_SEND);
		fi_bind(&ep->fid, &rcq->fid, FI_RECV);
		fi_enable(ep);
		fi_accept(ep, NULL, 0);
		fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0);
	} else {
		fi_getinfo(FI_VERSION(1, 0), dst_addr, port, 0, &hints, &info);
		fi_fabric(info->fabric_attr, &fab, NULL);
		fi_eq_open(fab, &cm_attr, &cmeq, NULL);
		fi_domain(fab, info->domain_attr, &dom, NULL);
		fi_endpoint(dom, info, &ep, NULL);
		fi_cq_open(dom, &cq_attr, &scq, NULL);
		fi_cq_open(dom, &cq_attr, &rcq, NULL);
		fi_mr_reg(dom, buf, buffer_size, 0, 0, 0, 0, &mr, NULL);
		fi_bind(&ep->fid, &cmeq->fid, 0);
		fi_bind(&ep->fid, &scq->fid, FI_SEND);
		fi_bind(&ep->fid, &rcq->fid, FI_RECV);
		fi_enable(ep);
		fi_connect(ep, info->dest_addr, NULL, 0);
		fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0);
	}

	if (server) {
		fi_recv(ep, buf, buffer_size, fi_mr_desc(mr), buf);
		while (!ret) {
			ret = fi_cq_read(rcq, &comp, sizeof comp);
			if (ret > 0) {
				printf("Contents in buffer: %s\n", (char*)buf);
			}
		};
	} else {
		strncpy((char*)buf, "Test String.", buffer_size);
		fi_send(ep, buf, buffer_size, fi_mr_desc(mr), NULL);
		while (!ret) {
			ret = fi_cq_read(scq, &comp, sizeof comp);
			if (ret > 0) {
				printf("Send done.\n");
			}
		};
	}

	fi_shutdown(ep, 0);
	fi_close(&mr->fid);
	fi_close(&rcq->fid);
	fi_close(&scq->fid);
	fi_close(&ep->fid);
	fi_close(&dom->fid);
	fi_close(&cmeq->fid);
	if (server) {
		fi_close(&pep->fid);
	}
	fi_close(&fab->fid);
	fi_freeinfo(info);
	free(buf);
	return ret;
}

