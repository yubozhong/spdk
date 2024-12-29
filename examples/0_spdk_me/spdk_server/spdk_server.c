/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   All rights reserved.
 */

#include <stdio.h>

#include "spdk/stdinc.h"
#include "spdk/thread.h"

#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/string.h"
#include "spdk/log.h"
#include "spdk/sock.h"

static bool g_running = true;
static int g_port;
static char * g_protocolP = NULL;
static char * g_host_strP;

#define ADDR_STR_LEN		INET6_ADDRSTRLEN
#define BUFFER_SIZE			1024

struct server_context_t {
	char* host;
	int port;
	char* sock_impl_name;

	int byte_in;
	int byte_out;

	struct spdk_sock * sock;
	struct spdk_sock_group *group;
};

static void spdk_server_shutdown_callback(void)
{
	g_running = false;
}

static int parse_arg_func(int ch, char * argv)
{
	printf("enter parse_arg_func ...\n");
	switch(ch)
	{
		case 'H': // 192.168.31.24
			g_host_strP = argv;
			printf("g_host_strP is parsed to %s \n", g_host_strP);
			break;

		case 'N': // POSIX
			g_protocolP  = argv;
			printf("g_protocolP is parsed to %s \n", g_protocolP);
			break;

		case 'P': // port 10086
			g_port = spdk_strtol(argv, 10);
			printf("port is parsed to %d \n", g_port);
			break;


		default:
			printf("arg  is parsed to error \n");
			return -EINVAL;
		
	}
	
	return 0;
	
}

static void print_help()
{
	printf("errror arg format \n");
}

#define BUFF_SIZE_ME 1024

static void spdk_proc_pkt_callback(void * arg, struct spdk_sock_group * group, struct spdk_sock *sock)
{
	struct server_context_t * ctx = arg;
	char buf[BUFF_SIZE_ME];
	struct iovec iov;

	ssize_t n = spdk_sock_recv(sock, buf, BUFF_SIZE_ME);
	if(n < 0)
		{
		if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
			SPDK_ERRLOG("errno %, %s \n", errno, spdk_strerror(errno));
			return 0;
		}
	}
	else if(!n)
	{
		SPDK_ERRLOG("recv closed \n");
		spdk_sock_group_remove_sock(ctx->group, sock);
		spdk_sock_close(&sock);
		return;
	}
	else
	{
		ctx->byte_in += n;
		iov.iov_base = buf;
		iov.iov_len =n;

		int n = spdk_sock_writev(sock, &iov, 1);
		if(n > 0)
		{
			ctx->byte_out += n;
		}

		return;
	}

	return 0;
}

static int spdk_server_accept(void *arg)
{
	struct server_context_t *ctx = arg;
	char saddr[ADDR_STR_LEN], daddr[ADDR_STR_LEN];
	uint16_t sport, dport;
	int count = 0;

	printf("enter spdk_server_accept ...\n");

	while(1)
	{
		struct spdk_sock * client_sock = spdk_sock_accept(ctx->sock);
		if(NULL == client_sock)
		{
			if(errno != EAGAIN && errno != EWOULDBLOCK)
				{} // ???
			
			break;
		}

		int rc = spdk_sock_getaddr(client_sock, saddr, sizeof(saddr), &sport, daddr, sizeof(daddr), &dport);
		if(rc < 0)
		{
			SPDK_ERRLOG("spdk_sock_getaddr\n");
			spdk_sock_close(&ctx->sock);
			return SPDK_POLLER_IDLE; // has done nothing
		}

		rc = spdk_sock_group_add_sock(ctx->group, client_sock, spdk_proc_pkt_callback, ctx); //
		if(rc < 0)
		{
				SPDK_ERRLOG("add pkt process failed ...\n");
				spdk_sock_close(&client_sock);
				return SPDK_POLLER_IDLE; // has done nothing
			
		}

		count++;
		
	}

	return count ? SPDK_POLLER_BUSY : SPDK_POLLER_IDLE;
}

static int spdk_server_group_pool(void * arg)
{
	struct server_context_t *ctx =  arg;

	printf("enter spdk_server_group_pool 11\n");
	int rc = spdk_sock_group_poll(ctx->group); // ???
	if(rc < 0)
		SPDK_ERRLOG("spdk_sock_group_poll failed \n");

	printf("enter spdk_server_group_pool 22\n");
	return rc > 0 ?  SPDK_POLLER_BUSY : SPDK_POLLER_IDLE;
}

static int spdk_server_listen(struct server_context_t *ctxP)
{
	ctxP->sock = spdk_sock_listen(ctxP->host, ctxP->port, ctxP->sock_impl_name);
	if( NULL == ctxP->sock)
	{
		printf("create listen sock failed ....\n");
		return -1;
	}

	ctxP->group = spdk_sock_group_create(NULL); // EPOLL,  what does this mean?
	g_running = true;

	SPDK_POLLER_REGISTER(spdk_server_accept, ctxP, 2000*1000); // 注册了accept, proc pkt 函数。

	SPDK_POLLER_REGISTER(spdk_server_group_pool, ctxP, 0); // ??? 这个不加上， 不会把收到的包发回来
	
	
	return 0;
}

static void spdk_server_start(void * arg)
{
	struct server_context_t *ctx = arg;
	printf("spdk_server_start ...\n");

	int rc = spdk_server_listen(ctx);
	if(rc)
		spdk_app_stop(-1);

	return;
}

int
main(int argc, char **argv)
{
	printf("hello spdk\n");

	struct spdk_app_opts opts = {};
	spdk_app_opts_init(&opts, sizeof(opts));

	opts.name = "spdk_server bob";
	opts.shutdown_cb = spdk_server_shutdown_callback;

	printf("spdk parsing args ...\n");

	spdk_app_parse_args(argc, argv, &opts, "H:N:P:", NULL, parse_arg_func, print_help);

	printf("spdk_app_parse_args done\n");

	struct server_context_t server_context = {};
	server_context.host = g_host_strP;
	server_context.port = g_port;
	server_context.sock_impl_name = g_protocolP;

	int rcv = spdk_app_start(&opts, spdk_server_start, &server_context);

	return 0;
}
