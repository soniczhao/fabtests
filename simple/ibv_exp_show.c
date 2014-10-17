/*
 * Copyright (c) 2014 NetApp, Inc.  All rights reserved.
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
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#define MAX_SEND_SGE 1
#define MAX_RECV_SGE 1

int main(int argc, char *argv[])
{
	int ret = 0;
	int op;
	char *dst_addr = NULL, *src_addr = NULL;
	bool server = true;
	char *port = "9228";
	void *buf = NULL;
	size_t buffer_size = 128;
	int max_credits = 128;

	struct ibv_context *context = NULL;
	struct rdma_event_channel *cm_channel = NULL;
	struct rdma_cm_id *cm_id_listen = NULL;
	struct rdma_cm_id *cm_id = NULL;
	struct rdma_cm_event *event = NULL;
	struct ibv_pd *pd = NULL;
	struct ibv_mr *mr = NULL;
	struct ibv_cq *send_cq = NULL;
	struct ibv_cq *recv_cq = NULL;
	struct ibv_qp *qp = NULL;
	struct ibv_qp_init_attr attr;
	struct rdma_conn_param conn_param;
	struct rdma_addrinfo hints;
	struct rdma_addrinfo *res = NULL;
	struct ibv_send_wr wr;
	struct ibv_recv_wr rwr;
	struct ibv_send_wr *bad_wr = NULL;
	struct ibv_recv_wr *bad_rwr = NULL;
	struct ibv_sge send_sgl;
	struct ibv_sge recv_sgl;
	struct ibv_wc wc;

	while ((op = getopt(argc, argv, "d:s:")) != -1) {
		switch (op) {
		case 'd':
			server = false;
			dst_addr = optarg;
			break;
		case 's':
			src_addr = optarg;
			break;
		}
	}

	buf = malloc(buffer_size);
	cm_channel = rdma_create_event_channel();
	if (server) {
		rdma_create_id(cm_channel, &cm_id_listen, NULL, RDMA_PS_TCP);
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = RAI_PASSIVE;
		hints.ai_port_space = RDMA_PS_IB;
		hints.ai_qp_type = IBV_QPT_RC;
		rdma_getaddrinfo(src_addr, port, &hints, &res);
		rdma_bind_addr(cm_id_listen, res->ai_src_addr);
		rdma_listen(cm_id_listen, 0);
		do {
			rdma_get_cm_event(cm_channel, &event);
		} while (event->event != RDMA_CM_EVENT_CONNECT_REQUEST);
		cm_id = (struct rdma_cm_id*)(event->id);
		context = cm_id->verbs;
		pd = ibv_alloc_pd(context);
		mr = ibv_reg_mr(pd, buf, buffer_size, IBV_ACCESS_LOCAL_WRITE);
		send_cq = ibv_create_cq(context, max_credits, NULL, NULL, 0);
		recv_cq = ibv_create_cq(context, max_credits, NULL, NULL, 0);
		memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
		attr.send_cq = send_cq;
		attr.recv_cq = recv_cq;
		attr.cap.max_send_wr  = max_credits;
		attr.cap.max_send_sge = MAX_SEND_SGE;
		attr.cap.max_inline_data = 0;
		attr.srq = NULL;
		attr.cap.max_recv_wr = max_credits;
		attr.cap.max_recv_sge = MAX_RECV_SGE;
		attr.qp_type = IBV_QPT_RC;
		rdma_create_qp(cm_id, pd, &attr);
		qp = cm_id->qp;
		memset(&conn_param, 0, sizeof(conn_param));
		conn_param.retry_count = 7;
		conn_param.rnr_retry_count = 7;
		rdma_accept(cm_id, &conn_param);
		rdma_ack_cm_event(event);
		memset(&rwr, 0, sizeof(rwr));
		memset(&recv_sgl, 0, sizeof(recv_sgl));
		recv_sgl.addr = (uintptr_t)buf;
		recv_sgl.length = buffer_size;
		recv_sgl.lkey = mr->lkey;
		rwr.sg_list = &recv_sgl;
		rwr.wr_id = 0;
		rwr.next = NULL;
		rwr.num_sge = 1;
		ibv_post_recv(qp, &rwr, &bad_rwr);
		memset(&wc, 0, sizeof(wc));
		while (!ret) {
			ret = ibv_poll_cq(recv_cq, 1, &wc);
		}
		printf("Contents in buffer: %s\n", (char*)buf);
	} else {
		rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
		memset(&hints, 0, sizeof(hints));
		hints.ai_port_space = RDMA_PS_IB;
		hints.ai_qp_type = IBV_QPT_RC;
		rdma_getaddrinfo(dst_addr, port, &hints, &res);
		rdma_resolve_addr(cm_id, NULL, res->ai_dst_addr, 2000);
		do {
			rdma_get_cm_event(cm_channel, &event);
		} while (event->event != RDMA_CM_EVENT_ADDR_RESOLVED);
		rdma_ack_cm_event(event);
		rdma_resolve_route(cm_id, 2000);
		do {
			rdma_get_cm_event(cm_channel, &event);
		} while (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED);
		rdma_ack_cm_event(event);
		context = cm_id->verbs;
		pd = ibv_alloc_pd(context);
		mr = ibv_reg_mr(pd, buf, buffer_size, IBV_ACCESS_LOCAL_WRITE);
		send_cq = ibv_create_cq(context, max_credits << 1, NULL, NULL, 0);
		recv_cq = ibv_create_cq(context, max_credits << 1, NULL, NULL, 0);
		memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
		attr.send_cq = send_cq;
		attr.recv_cq = recv_cq;
		attr.cap.max_send_wr  = max_credits;
		attr.cap.max_send_sge = MAX_SEND_SGE;
		attr.cap.max_inline_data = 0;
		attr.srq = NULL;
		attr.cap.max_recv_wr = max_credits;
		attr.cap.max_recv_sge = MAX_RECV_SGE;
		attr.qp_type = IBV_QPT_RC;
		rdma_create_qp(cm_id, pd, &attr);
		qp = cm_id->qp;
		memset(&conn_param, 0, sizeof(conn_param));
		rdma_connect(cm_id, &conn_param);
		do {
			rdma_get_cm_event(cm_channel, &event);
		} while (event->event != RDMA_CM_EVENT_ESTABLISHED);
		rdma_ack_cm_event(event);

		strncpy((char*)buf, "Marry had a little lamb, his fleece was white as snow.", buffer_size);
		printf("Contents in buffer: %s\n", (char*)buf);
		memset(&wr, 0, sizeof(wr));
		memset(&send_sgl, 0, sizeof(send_sgl));
		send_sgl.addr = (uintptr_t)buf;
		send_sgl.length = buffer_size;
		send_sgl.lkey = mr->lkey;
		wr.sg_list = &send_sgl;
		wr.wr_id = 0;
		wr.next = NULL;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_SEND;
		wr.send_flags = IBV_SEND_SIGNALED;
		ibv_post_send(qp, &wr, &bad_wr);
		memset(&wc, 0, sizeof(wc));
		while (!ret) {
			ret = ibv_poll_cq(send_cq, 1, &wc);
		}
	}

	rdma_disconnect(cm_id);
	ibv_destroy_qp(qp);
	ibv_destroy_cq(recv_cq);
	ibv_destroy_cq(send_cq);
	ibv_dereg_mr(mr);
	ibv_dealloc_pd(pd);
	rdma_destroy_id(cm_id);
	rdma_freeaddrinfo(res);
	if (server) {
		rdma_destroy_id(cm_id_listen);
	}
	rdma_destroy_event_channel(cm_channel);
	if (buf) {
		free(buf);
		buf = NULL;
	}
	return ret;
}

