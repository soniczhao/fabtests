/*
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_rma.h>
#include "../common/shared.h"

#define MIN_BUF_SIZE 128
#define BW_DOMAIN_NAME "FI_WRITE_BW"

static bool custom = false;
static bool client = false;
static bool bidir = false;
static bool custom_iterations = false;

static int size_option = 1;
static int iterations;
static int transfer_size;
static int max_credits = 128;
static int send_credits = 128;
static int recv_credits = 128;
static struct timeval start, end;
static void *buf;
static uint64_t rembuf;
static uint64_t rkey;
static size_t buffer_size;

static struct fi_info hints;
static struct fi_domain_attr domain_hints;
static struct fi_ep_attr ep_hints;
static char *dst_addr, *src_addr;
static char *port = "9228";

static struct fid_fabric *fab;
static struct fid_pep *pep;
static struct fid_domain *dom;
static struct fid_ep *ep;
static struct fid_eq *cmeq;
static struct fid_cq *rcq, *scq;
static struct fid_mr *mr;

static void show_perf(void)
{
	char str[32];
	float usec;
	long long bytes;

	usec = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
	bytes = (long long) iterations * transfer_size;

	/* name size transfers iterations bytes seconds Gb/sec usec/xfer */
	size_str(str, sizeof str, transfer_size);
	printf("%-8s", str);
	cnt_str(str, sizeof str, iterations);
	printf("%-8s", str);
	size_str(str, sizeof str, bytes);
	printf("%-8s", str);
	printf("%8.2fs%10.2f%11.2f\n",
		usec / 1000000., (bytes) / (usec),
		(usec / iterations));
}

static void init_test(int size)
{
	transfer_size = size;
	if (!custom_iterations) {
		iterations = size_to_count(transfer_size);
	}
}

static int poll_all_sends(void)
{
	struct fi_cq_entry comp;
	int ret;

	while (send_credits < max_credits) {
		ret = fi_cq_read(scq, &comp, sizeof comp);
		if (ret > 0) {
			send_credits++;
		} else if (ret < 0) {
			printf("Event queue read %d (%s)\n", ret, fi_strerror(-ret));
			return ret;
		}
	}
	return 0;
}

static int poll_all_recvs(void)
{
	struct fi_cq_entry comp;
	int ret;

	while (recv_credits < max_credits) {
		ret = fi_cq_read(rcq, &comp, sizeof comp);
		if (ret > 0) {
			recv_credits++;
		} else if (ret < 0) {
			printf("Event queue read %d (%s)\n", ret, fi_strerror(-ret));
			return ret;
		}
	}
	return 0;
}

static int write_xfer(int size)
{
	struct fi_cq_entry comp;
	int ret;

	while (!send_credits) {
		ret = fi_cq_read(scq, &comp, sizeof comp);
		if (ret > 0) {
			goto post;
		} else if (ret < 0) {
			printf("Event queue read %d (%s)\n", ret, fi_strerror(-ret));
			return ret;
		}
	}

	send_credits--;
post:
	ret = fi_write(ep, buf, (size_t) size, fi_mr_desc(mr), rembuf, rkey, NULL);
	if (ret)
		printf("fi_write %d (%s)\n", ret, fi_strerror(-ret));

	return ret;
}

static int send_xfer(int size)
{
	struct fi_cq_entry comp;
	int ret;

	while (!send_credits) {
		ret = fi_cq_read(scq, &comp, sizeof comp);
		if (ret > 0) {
			goto post;
		} else if (ret < 0) {
			printf("Event queue read %d (%s)\n", ret, fi_strerror(-ret));
			return ret;
		}
	}

	send_credits--;
post:
	ret = fi_send(ep, buf, (size_t) size, fi_mr_desc(mr), NULL);
	if (ret)
		printf("fi_send %d (%s)\n", ret, fi_strerror(-ret));

	return ret;
}

static int recv_xfer(int size)
{
	struct fi_cq_entry comp;
	int ret;

	while (!recv_credits) {
		ret = fi_cq_read(rcq, &comp, sizeof comp);
		if (ret > 0) {
			goto post;
		} else if (ret < 0) {
			printf("Event queue read %d (%s)\n", ret, fi_strerror(-ret));
			return ret;
		}
	}

	recv_credits--;
post:
	ret = fi_recv(ep, buf, buffer_size, fi_mr_desc(mr), buf);
	if (ret)
		printf("fi_recv %d (%s)\n", ret, fi_strerror(-ret));

	return ret;
}

static int sync_test(void)
{
	int ret = 0;

	if (client) {
		*((uint64_t *)buf) = (uint64_t)buf;
		*((uint64_t *)buf + 1) = fi_mr_key(mr);
		if ((ret = send_xfer(sizeof(uint64_t)*2))) {
			return ret;
		}
		if ((ret = poll_all_sends())) {
			return ret;
		}
		if ((ret = recv_xfer(sizeof(uint64_t)*2))) {
			return ret;
		}
		if ((ret = poll_all_recvs())) {
			return ret;
		}
		rembuf = *((uint64_t *)buf);
		rkey = *((uint64_t *)buf + 1);
	} else {
		if ((ret = recv_xfer(sizeof(uint64_t)*2))) {
			return ret;
		}
		if ((ret = poll_all_recvs())) {
			return ret;
		}
		rembuf = *((uint64_t *)buf);
		rkey = *((uint64_t *)buf + 1);
		*((uint64_t *)buf) = (uint64_t)buf;
		*((uint64_t *)buf + 1) = fi_mr_key(mr);
		if ((ret = send_xfer(sizeof(uint64_t)*2))) {
			return ret;
		}
		if ((ret = poll_all_sends())) {
			return ret;
		}
	}

	return ret;
}

static int run_test(void)
{
	int ret = 0, i;

	if ((ret = sync_test())) {
		goto out;
	}

	if (bidir || client) {
		gettimeofday(&start, NULL);
		for (i = 0; i < iterations; i++) {
			if ((ret = write_xfer(transfer_size))) {
				goto out;
			}
		}
		if ((ret = poll_all_sends())) {
			goto out;
		}
		gettimeofday(&end, NULL);
		show_perf();
	}

	if ((ret = sync_test())) {
		goto out;
	}

out:
	return ret;
}

static void free_lres(void)
{
	fi_close(&cmeq->fid);
}

static int alloc_cm_res(void)
{
	struct fi_eq_attr cm_attr;
	int ret;

	memset(&cm_attr, 0, sizeof cm_attr);
	cm_attr.wait_obj = FI_WAIT_FD;
	ret = fi_eq_open(fab, &cm_attr, &cmeq, NULL);
	if (ret)
		printf("fi_eq_open cm %s\n", fi_strerror(-ret));

	return ret;
}

static void free_ep_res(void)
{
	fi_close(&mr->fid);
	fi_close(&rcq->fid);
	fi_close(&scq->fid);
	free(buf);
}

static int alloc_ep_res(struct fi_info *fi)
{
	struct fi_cq_attr cq_attr;
	int ret;

	buffer_size = !custom ? test_size[TEST_CNT - 1].size : transfer_size;
	if (buffer_size < MIN_BUF_SIZE) {
		buffer_size = MIN_BUF_SIZE;
	}
	buf = malloc(buffer_size);
	if (!buf) {
		perror("malloc");
		return -1;
	}

	memset(&cq_attr, 0, sizeof cq_attr);
	cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	cq_attr.wait_obj = FI_WAIT_NONE;
	cq_attr.size = max_credits << 1;
	ret = fi_cq_open(dom, &cq_attr, &scq, NULL);
	if (ret) {
		printf("fi_eq_open send comp %s\n", fi_strerror(-ret));
		goto err1;
	}

	ret = fi_cq_open(dom, &cq_attr, &rcq, NULL);
	if (ret) {
		printf("fi_eq_open recv comp %s\n", fi_strerror(-ret));
		goto err2;
	}

	ret = fi_mr_reg(dom, buf, buffer_size, FI_REMOTE_WRITE, 0, 0, 0, &mr, NULL);
	if (ret) {
		printf("fi_mr_reg %s\n", fi_strerror(-ret));
		goto err3;
	}

	if (!cmeq) {
		ret = alloc_cm_res();
		if (ret)
			goto err4;
	}

	return 0;

err4:
	fi_close(&mr->fid);
err3:
	fi_close(&rcq->fid);
err2:
	fi_close(&scq->fid);
err1:
	free(buf);
	return ret;
}

static int bind_ep_res(void)
{
	int ret;

	ret = bind_fid(&ep->fid, &cmeq->fid, 0);
	if (ret)
		return ret;

	ret = bind_fid(&ep->fid, &scq->fid, FI_SEND);
	if (ret)
		return ret;

	ret = bind_fid(&ep->fid, &rcq->fid, FI_RECV);
	if (ret)
		return ret;

	ret = fi_enable(ep);
	if (ret)
		return ret;

	return ret;
}

static int server_listen(void)
{
	struct fi_info *fi;
	int ret;

	hints.ep_cap |= FI_PASSIVE;
	ret = fi_getinfo(FI_VERSION(1, 0), src_addr, port, 0, &hints, &fi);
	if (ret) {
		printf("fi_getinfo %s\n", strerror(-ret));
		return ret;
	}

	ret = fi_fabric(fi->fabric_attr, &fab, NULL);
	if (ret) {
		printf("fi_fabric %s\n", fi_strerror(-ret));
		goto err0;
	}

	ret = fi_pendpoint(fab, fi, &pep, NULL);
	if (ret) {
		printf("fi_endpoint %s\n", fi_strerror(-ret));
		goto err1;
	}

	ret = alloc_cm_res();
	if (ret)
		goto err2;

	ret = bind_fid(&pep->fid, &cmeq->fid, 0);
	if (ret)
		goto err3;

	ret = fi_listen(pep);
	if (ret) {
		printf("fi_listen %s\n", fi_strerror(-ret));
		goto err3;
	}

	fi_freeinfo(fi);
	return 0;
err3:
	free_lres();
err2:
	fi_close(&pep->fid);
err1:
	fi_close(&fab->fid);
err0:
	fi_freeinfo(fi);
	return ret;
}

static int server_connect(void)
{
	struct fi_eq_cm_entry entry;
	enum fi_eq_event event;
	struct fi_info *info = NULL;
	ssize_t rd;
	int ret;

	rd = fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0);
	if (rd != sizeof entry) {
		printf("fi_eq_sread %zd %s\n", rd, fi_strerror((int) -rd));
		return (int) rd;
	}

	if (event != FI_CONNREQ) {
		printf("Unexpected CM event %d\n", event);
		ret = -FI_EOTHER;
		goto err1;
	}

	info = entry.info;
	ret = fi_domain(fab, info->domain_attr, &dom, NULL);
	if (ret) {
		printf("fi_domain %s\n", fi_strerror(-ret));
		goto err1;
	}

	ret = fi_endpoint(dom, info, &ep, NULL);
	if (ret) {
		printf("fi_endpoint for req %s\n", fi_strerror(-ret));
		goto err1;
	}

	ret = alloc_ep_res(info);
	if (ret)
		 goto err2;

	ret = bind_ep_res();
	if (ret)
		goto err3;

	ret = fi_accept(ep, NULL, 0);
	if (ret) {
		printf("fi_accept %s\n", fi_strerror(-ret));
		goto err3;
	}

	rd = fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0);
	if (rd != sizeof entry) {
		printf("fi_eq_sread %zd %s\n", rd, fi_strerror((int) -rd));
		goto err3;
	}

	if (event != FI_COMPLETE || entry.fid != &ep->fid) {
		printf("Unexpected CM event %d fid %p (ep %p)\n",
			event, entry.fid, ep);
		ret = -FI_EOTHER;
		goto err3;
	}

	fi_freeinfo(info);
	return 0;

err3:
	free_ep_res();
err2:
	fi_close(&ep->fid);
err1:
	fi_reject(pep, info->connreq, NULL, 0);
	fi_freeinfo(info);
	return ret;
}

static int client_connect(void)
{
	struct fi_eq_cm_entry entry;
	enum fi_eq_event event;
	struct fi_info *fi;
	ssize_t rd;
	int ret;

	if (src_addr) {
		ret = getaddr(src_addr, NULL, (struct sockaddr **) &hints.src_addr,
			      (socklen_t *) &hints.src_addrlen);
		if (ret)
			printf("source address error %s\n", gai_strerror(ret));
	}

	ret = fi_getinfo(FI_VERSION(1, 0), dst_addr, port, 0, &hints, &fi);
	if (ret) {
		printf("fi_getinfo %s\n", strerror(-ret));
		goto err0;
	}

	ret = fi_fabric(fi->fabric_attr, &fab, NULL);
	if (ret) {
		printf("fi_fabric %s\n", fi_strerror(-ret));
		goto err1;
	}

	ret = fi_domain(fab, fi->domain_attr, &dom, NULL);
	if (ret) {
		printf("fi_domain %s %s\n", fi_strerror(-ret),
			fi->domain_attr->name);
		goto err2;
	}

	ret = fi_endpoint(dom, fi, &ep, NULL);
	if (ret) {
		printf("fi_endpoint %s\n", fi_strerror(-ret));
		goto err3;
	}

	ret = alloc_ep_res(fi);
	if (ret)
		goto err4;

	ret = bind_ep_res();
	if (ret)
		goto err5;

	ret = fi_connect(ep, fi->dest_addr, NULL, 0);
	if (ret) {
		printf("fi_connect %s\n", fi_strerror(-ret));
		goto err5;
	}

	rd = fi_eq_sread(cmeq, &event, &entry, sizeof entry, -1, 0);
	if (rd != sizeof entry) {
		printf("fi_eq_sread %zd %s\n", rd, fi_strerror((int) -rd));
		return (int) rd;
	}

	if (event != FI_COMPLETE || entry.fid != &ep->fid) {
		printf("Unexpected CM event %d fid %p (ep %p)\n",
			event, entry.fid, ep);
		ret = -FI_EOTHER;
		goto err1;
	}

	if (hints.src_addr)
		free(hints.src_addr);
	fi_freeinfo(fi);
	return 0;

err5:
	free_ep_res();
err4:
	fi_close(&ep->fid);
err3:
	fi_close(&dom->fid);
err2:
	fi_close(&fab->fid);
err1:
	fi_freeinfo(fi);
err0:
	if (hints.src_addr)
		free(hints.src_addr);
	return ret;
}

static int run(void)
{
	int i, ret = 0;

	if (!client) {
		ret = server_listen();
		if (ret)
			return ret;
	}

	printf("%-8s%-8s%-8s%8s %10s%13s\n",
	       "bytes", "iters", "total", "time", "MB/sec", "usec/xfer");

	ret = client ? client_connect() : server_connect();
	if (ret)
		return ret;

	if (!custom) {
		for (i = 0; i < TEST_CNT; i++) {
			if (test_size[i].option > size_option)
				continue;
			init_test(test_size[i].size);
			ret = run_test();
		}
	} else {

		ret = run_test();
	}

	fi_shutdown(ep, 0);
	fi_close(&ep->fid);
	free_ep_res();
	if (!client)
		free_lres();
	fi_close(&dom->fid);
	fi_close(&fab->fid);
	return ret;
}

int main(int argc, char **argv)
{
	int op, ret;

	while ((op = getopt(argc, argv, "d:p:s:C:I:S:b")) != -1) {
		switch (op) {
		case 'd':
			dst_addr = optarg;
			client = true;
			break;
		case 'p':
			port = optarg;
			break;
		case 's':
			src_addr = optarg;
			break;
		case 'I':
			custom_iterations = true;
			iterations = atoi(optarg);
			break;
		case 'S':
			if (!strncasecmp("all", optarg, 3)) {
				size_option = 1;
			} else if (!strncasecmp("ext", optarg, 3)) {
				size_option = 2;
			} else {
				custom = 1;
				transfer_size = atoi(optarg);
			}
			break;
		case 'b':
			bidir = true;
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-d destination_address] (client only)\n");
			printf("\t[-p port_number] (default: 9228)\n");
			printf("\t[-s source_address]\n");
			printf("\t[-I iterations] (default: dynamic)\n");
			printf("\t[-S transfer_size or 'all' or 'ext'] (default: all)\n");
			printf("\t[-b ] Bidirectional transfer (default: disabled)\n");
			exit(1);
		}
	}

	hints.domain_attr = &domain_hints;
	hints.ep_attr = &ep_hints;
	hints.type = FI_EP_MSG;
	hints.ep_cap = FI_RMA | FI_MSG;
	domain_hints.caps = FI_LOCAL_MR;
	domain_hints.name = BW_DOMAIN_NAME;
	hints.addr_format = FI_SOCKADDR;

	ret = run();
	return ret;
}
