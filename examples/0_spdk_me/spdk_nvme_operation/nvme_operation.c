/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2016 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/vmd.h"
#include "spdk/nvme_zns.h"
#include "spdk/env.h"
#include "spdk/string.h"
#include "spdk/log.h"

//#include "spdk/nvme_spec.h"

//
//#include "spdk/stdinc.h"
//#include "spdk/nvme.h"
//#include "spdk/vmd.h"
//#include "spdk/nvme_zns.h"
//
//#include "spdk/env.h"
//#include "spdk/string.h"
//#include "spdk/log.h"



#define DATA_BUFFER_STRING "Hello world!"

#ifndef bool
#define bool int
#endif


#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

struct ctrlr_entry{
	struct spdk_nvme_ctrlr * ctrlr;
	TAILQ_ENTRY(ctrlr_entry) link;
	char name[1024];
};

struct ns_entry{
	struct spdk_nvme_ctrlr * ctrlr;
	struct spdk_nvme_ns * ns;

	TAILQ_ENTRY(ns_entry) link;
	struct spdk_nvme_qpair * qpair;
};

struct spdk_nvme_sequence{
	struct ns_entry * ns_entry;
	char * buf;

	unsigned using_cmb_io;
	int is_completed;

};

static TAILQ_HEAD(, ctrlr_entry) g_controllers = TAILQ_HEAD_INITIALIZER(g_controllers);
static TAILQ_HEAD(, ctrlr_entry) g_namespaces = TAILQ_HEAD_INITIALIZER(g_namespaces);


static bool g_vmd = true; // default, enable VMD device, VMD must be used 
static struct spdk_nvme_transport_id  gtrid = {};

#define NVME_PCI_NUME "traddr-0000:03:0.0"


static bool _my_spdk_nvme_probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
				   struct spdk_nvme_ctrlr_opts *opts)
{
	printf("enter _my_spdk_nvme_probe_cb, attach to %s\n", trid->traddr);

	return true;
}

static void spdk_nvme_register_ns(struct spdk_nvme_ctrlr * ctrlr, struct spdk_nvme_ns *ns)
{
	if(!spdk_nvme_ns_is_active(ns))
		return;

	struct ns_entry * entry = malloc(sizeof(struct ns_entry));
	if(NULL == entry)
	{
		perror("spdk_nvme_register_ns malloc entry failed\n");
		exit(1);
	}

	entry->ctrlr = ctrlr;
	entry->ns = ns;

	TAILQ_INSERT_TAIL(&g_namespaces, entry, link); // 这就结束了， 后台如何工作

	printf("name space id %d size: %juGB\n", spdk_nvme_ns_get_id(ns), spdk_nvme_ns_get_size(ns)/1024/1024/1024);
}

static void spdk_nvme_reset_zone_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct spdk_nvme_sequence * seq = arg;
	seq->is_completed = 1;

	if(spdk_nvme_cpl_is_error(completion))
	{
		spdk_nvme_qpair_print_completion(seq->ns_entry->qpair, completion);
		printf("sequece complete error: %s ", spdk_nvme_cpl_get_status_string(&completion->status));
		seq->is_completed = 2;
		exit(1);
	}
}

static void spdk_nvme_reset_zone_and_wait_for_complete(struct spdk_nvme_sequence * sequence) // /??????
{
	if(spdk_nvme_zns_reset_zone(sequence->ns_entry->ns, sequence->ns_entry->qpair, 0, false, spdk_nvme_reset_zone_complete, sequence))
	{
		printf("spdk_nvme_zns_reset_zone failed.\n");
		exit(1);
	}

	while(!sequence->is_completed)
	{
		spdk_nvme_qpair_process_completions(sequence->ns_entry->qpair, 0); //
	}

	sequence->is_completed = 0;
	
	
}

static void read_complete_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_nvme_sequence * seq = ctx;
	seq->is_completed = 1;

	if (spdk_nvme_cpl_is_error(cpl)) {
		spdk_nvme_qpair_print_completion(seq->ns_entry->qpair, (struct spdk_nvme_cpl*)cpl);
		fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
		fprintf(stderr, "Read I/O failed, aborting run\n");
		seq->is_completed = 2;
		exit(1);
	}

	printf("read completed, content is : %s\n", seq->buf);
	spdk_free(seq->buf);
}

static void write_complete_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_nvme_sequence * seq = ctx;
	struct ns_entry * ns_entry = seq->ns_entry;

	if(spdk_nvme_cpl_is_error(cpl))
	{
		spdk_nvme_qpair_print_completion(ns_entry->qpair, cpl);
		printf("error write complete error: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
		seq->is_completed = 2;
		exit(1);
	}

	// check whether seq->buf still exist or not?
	printf(" 11 in write_complete_cb seq->buf is %s \n", seq->buf);
	printf("free  seq->buf \n");

	spdk_free(seq->buf);

	//seq->is_completed = 1; // delete, move to read complete

	// read back . todo
	seq->buf = spdk_zmalloc(0x1000, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
	int rc = spdk_nvme_ns_cmd_read(seq->ns_entry->ns, seq->ns_entry->qpair, seq->buf, 
									5, 1, read_complete_cb, (void*)seq, 0);


	if(rc != 0)
	{
		printf("Error: read failed \n");
		exit(1);
	}
	
}

static void spdk_nvme_test(void)
{
	struct ns_entry * ns_entry;
	struct spdk_nvme_sequence sequence;
	size_t sz;

	TAILQ_FOREACH(ns_entry, &g_namespaces, link){
		ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0); // 最后为什么是0， size?
		if(NULL == ns_entry->qpair)
		{
			printf("spdk_nvme_test fail 1\n");
			return;
		}

		sequence.using_cmb_io = 1;
		sequence.buf = spdk_nvme_ctrlr_map_cmb(ns_entry->ctrlr, &sz);

		if(NULL == sequence.buf || sz < 0x1000)
		{
			printf("sequence.buf is null from cmb .\n");
			sequence.using_cmb_io = 0;
			sequence.buf = spdk_zmalloc(0x1000, 0x1000, NULL, 
				SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
		}

		printf("spdk_nvme_test sz is %d \n ", sz);

		if(NULL == sequence.buf)
		{
			printf("sequence.buf is null error . \n");
			return;
		}

		sequence.is_completed = 0;
		sequence.ns_entry = ns_entry;

		printf("ns csi is : %d compared to 2 \n", spdk_nvme_ns_get_csi(ns_entry->ns));

		
		/*
		 * If the namespace is a Zoned Namespace, rather than a regular
		 * NVM namespace, we need to reset the first zone, before we
		 * write to it. This not needed for regular NVM namespaces.
		 */
		if(spdk_nvme_ns_get_csi(ns_entry->ns) == SPDK_NVME_CSI_ZNS) // get command set identifier? 
		{
			// SPDK_NVME_CSI_ZNS  what's the meaning ???

			spdk_nvme_reset_zone_and_wait_for_complete(&sequence);
		}

		// write test
		snprintf(sequence.buf, 0x1000, "%s", "test nvme write 1111" );
		printf("start to write : %s \n", sequence.buf);
		int rc = spdk_nvme_ns_cmd_write(ns_entry->ns, ns_entry->qpair, 
										sequence.buf, 
										5 /*uint64_t lba*/, // 逻辑块的地址。影响写的位置
										1 /* uint32_t lba_count */,
										write_complete_cb, &sequence, 0 /*uint32_t io_flags*/);

		if(rc != 0)
		{
			printf("Error : write failed \n");
			exit(1);
		}

		while(!sequence.is_completed)
		{
			spdk_nvme_qpair_process_completions(ns_entry->qpair,  0 /*uint32_t max_completions*/);
		}

		spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);		
		
	}
}

static void _my_spdk_nvme_attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
				    struct spdk_nvme_ctrlr *ctrlr,
				    const struct spdk_nvme_ctrlr_opts *opts)
{
	printf("enter _my_spdk_nvme_attach_cb, %s is attached successfully. \n", trid->traddr);

	struct ctrlr_entry * entry = malloc(sizeof(struct ctrlr_entry ));
	if(NULL == entry)
	{
		perror("_my_spdk_nvme_attach_cb malloc entry failed\n");
		exit(1);
	}

	const struct spdk_nvme_ctrlr_data * cdata = spdk_nvme_ctrlr_get_data(ctrlr);
	snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);
	printf("entry->name : %s \n", entry->name);
	entry->ctrlr = ctrlr;

	TAILQ_INSERT_TAIL(&g_controllers, entry, link);

	int nsid;
	for(nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr); nsid != 0; 
	nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid) )
	{
		printf("nsid = %d \n", nsid);
		struct spdk_nvme_ns * ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if(NULL == ns) continue;

		spdk_nvme_register_ns(ctrlr, ns); // ???? what 
	}

	
	spdk_nvme_test();

}

					
static void cleanup()
{
	struct ns_entry * ns_entry, *tmp_ns_entry;
	struct ctrlr_entry * ctrlr_entry, * tmp_ctrlr_entry;

	struct spdk_nvme_detach_ctx * detach_ctx = NULL;

	
	TAILQ_FOREACH_SAFE(ns_entry, &g_namespaces, link, tmp_ns_entry) {
	
			TAILQ_REMOVE(&g_namespaces, ns_entry, link);
			free(ns_entry);
			
		}
	
		printf("[%s:%d] --> cleanup\n", __func__, __LINE__);
	
		TAILQ_FOREACH_SAFE(ctrlr_entry, &g_controllers, link, tmp_ctrlr_entry) {
	
			TAILQ_REMOVE(&g_controllers, ctrlr_entry, link);
			spdk_nvme_detach_async(ctrlr_entry->ctrlr, &detach_ctx);
			free(ctrlr_entry);
	
		}
	
		if (detach_ctx) {
	
			spdk_nvme_detach_poll(detach_ctx);
	
		}

}

int
main(int argc, char **argv)
{
	int rc;
	struct spdk_env_opts opts;

	char * ctxp = NULL;

	/*
	 * SPDK relies on an abstraction around the local environment
	 * named env that handles memory allocation and PCI device operations.
	 * This library must be initialized first.
	 *
	 */
	opts.opts_size = sizeof(opts);
	spdk_env_opts_init(&opts);
	if (rc != 0) {
		return rc;
	}

	opts.name = "nvme operation";
	if (spdk_env_init(&opts) < 0) {
		fprintf(stderr, "Unable to initialize SPDK env\n");
		SPDK_ERRLOG("INIT FAILED.\n");
		return 1;
	}

	printf("nvme operations: spdk_env_init done\n");
	
	if(g_vmd && spdk_vmd_init())
	{
		SPDK_ERRLOG("spdk_vmd_init init failed. error \n");
	}
	else
		printf("spdk_vmd_init init done.\n");

	{
		// set pci number
		spdk_nvme_trid_populate_transport(&gtrid, SPDK_NVME_TRANSPORT_PCIE);
		snprintf(gtrid.subnqn, sizeof(gtrid.subnqn), SPDK_NVMF_DISCOVERY_NQN);

		//spdk_nvme_transport_id_parse(&gtrid, "03:00.0"  ); // NVME_PCI_NUME
        strcpy(gtrid.traddr, "03:00.0");
	}

//	rc = spdk_nvme_probe(&gtrid, ctxp, _my_spdk_nvme_probe_cb, _my_spdk_nvme_attach_cb, NULL);
	rc = spdk_nvme_probe(NULL, ctxp, _my_spdk_nvme_probe_cb, _my_spdk_nvme_attach_cb, NULL);

	if (rc != 0) {
		printf("spdk_nvme_probe failed %d \n", rc);
		goto exit;
	}

	printf("spdk_nvme_probe ok, return %d \n", rc);
	

exit:
	fflush(stdout);
	cleanup();
	if (g_vmd) {
		spdk_vmd_fini();
	}

//	spdk_env_fini();
	return rc;
}
