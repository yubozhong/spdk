
#include "spdk/event.h"
#include "spdk/log.h" 
#include "spdk/env.h"

#include "spdk/bdev.h"
#include "spdk/blob.h"
#include "spdk/blob_bdev.h"

//问题：
// blob, bdev 的关系 ？


typedef struct bob_fs_thread {
	struct spdk_thread * thread;
} bob_fs_thread_t;

bob_fs_thread_t * g_fs_thread = NULL;


// how to get json
// /mnt/spdk_practice/spdk/scripts# ./gen_nvme.sh --json-with-subsystems > bob_nvme.json
static const char * json_file = "spdk_subsystem_init_from_json_config/bob_nvme.json"; // 等下在哪儿生成一下。

static void json_app_load_done(int rc, void * ctx)
{
	bool * done = ctx;

	printf("json app load done\n");
	*done= true;
}

static void bobfs_bootstrap_fn(void *arg)
{
	// 这个函数哪来的？ 编译Link不了
	spdk_subsystem_init(spdk_subsystem_init_fn cb_fn, void * cb_arg);

	spdk_subsystem_i
	
	//spdk_subsystem_init_from_json_config(json_file, SPDK_DEFAULT_RPC_ADDR,
	                       json_app_load_done, arg);
}

#define POLLER_MAX_TIME 1e8

static bool poller (struct spdk_thread *thread, spdk_msg_fn fn, /*回调函数指针*/
                        void * ctx, bool * done)
{
	int poll_cnt = 0;

	// bobfs_bootstrap_fn is thread body
	spdk_thread_send_msg(thread, fn, ctx); // do what ?

	do{
		spdk_thread_poll(thread, 0, 0);
		poll_cnt++;
	}while(!(*done) && (poll_cnt < POLLER_MAX_TIME));

	return true;
}

int main(int argc, char*arv[])
{
	bob_fs_thread_t fs_thread;
	g_fs_thread = &fs_thread; // 保存thread handle


	// start init
	{
		struct spdk_env_opts opts;
		spdk_env_opts_init(&opts);

		if(0 != spdk_env_init(&opts))
		{
			SPDK_ERRLOG("spdk_env_init failed \n");
		}
		printf("spdk_env_init DONE \n");

		spdk_log_set_print_level(SPDK_LOG_DEBUG);
		spdk_log_set_level(SPDK_LOG_DEBUG);
		spdk_log_open(NULL);

		// set thread
		spdk_thread_lib_init(NULL, 0); // do what ?
		g_fs_thread->thread = spdk_thread_create("bobfs", NULL);
		spdk_set_thread(g_fs_thread->thread); // do what, 赋值给一个全局变量， 什么用。

		bool done = false;
		poller(g_fs_thread->thread, bobfs_bootstrap_fn, &done, &done); // do what ?
		
	}

	// todo: blob 操作 
	{

	}

	// end init

	return 0;
}







