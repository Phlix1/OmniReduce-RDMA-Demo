#include "common.h"

struct config_t server_config = {
	NULL,  /* dev_name */
	NULL,  /* server_name */
	19875, /* tcp_port */
	1,	 /* ib_port */
	-1, /* gid_idx */
	0   /* service level */
};
void handle_recv(struct resources *res)
{
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    uint32_t max_index = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE;
    uint32_t next_offset = 0;
    struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
    uint32_t current_offset[NUM_SLOTS*NUM_THREADS];
    for(int i=0; i<NUM_SLOTS; i++){
        current_offset[i] = start_offset+i*MESSAGE_SIZE;
        post_receive_server(res, current_offset[i], i+NUM_SLOTS*res->threadId);
    }
    while (1) {
        int ne = ibv_poll_cq(res->cq[res->threadId], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
	if (ne>0){
            for (int i = 0; i < ne; ++i)
	    {
	        if (wc[i].status == IBV_WC_SUCCESS)	
		{
		    if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
	                uint32_t imm_data = wc[i].imm_data;
                        next_offset = imm_data;
			int ret = 0;
			int slot = 0;
			slot = (next_offset/MESSAGE_SIZE)%NUM_SLOTS;
#ifdef DEBUG
			fprintf(stdout, "%d %d\n", next_offset/MESSAGE_SIZE, (next_offset/MESSAGE_SIZE)%NUM_SLOTS);
			fprintf(stdout, "threadid: %d; slot: %d; current offset: %d; nextoffset: %d; message size:%d; num slots:%d\n", res->threadId, slot+NUM_SLOTS*res->threadId, current_offset[slot], next_offset, MESSAGE_SIZE, NUM_SLOTS);
			fprintf(stdout, "after receiving :%c, %c, %c, %c, %c, %c, %c, %c, %c, %c\n", res->buf[0], res->buf[1], res->buf[2], res->buf[3], res->buf[4], res->buf[5], res->buf[6], res->buf[7], res->buf[8], res->buf[9]);
#endif
			post_receive_server(res, current_offset[slot], slot+NUM_SLOTS*res->threadId);
			ret = post_send_server(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, current_offset[slot], next_offset, slot+NUM_SLOTS*res->threadId);
			if(ret)
			{
			    fprintf(stderr, "failed to post SR\n");
		            exit(1);
			}
			if (next_offset<max_index)
			    current_offset[slot] = next_offset;
			else
			    current_offset[slot] = start_offset+slot*MESSAGE_SIZE;
		    }												                        }
	    }
	}
    }
}
void *process_per_thread(void *arg)
{
    struct resources *res = (struct resources *)arg;
    handle_recv(res);
}
/*****************************************************************************
* Function: main
*
* Input
* argc number of items in argv
* argv command line parameters
*
* Output
* none
*
* Returns
* 0 on success, 1 on failure
*
* Description
* Main program code
******************************************************************************/
int main(int argc, char *argv[])
{
	struct resources res;
	int rc = 1;
	/* parse the command line parameters */
	while (1)
	{
		int c;
		static struct option long_options_tmp[8];
		long_options_tmp[0].name="port";long_options_tmp[0].has_arg=1;long_options_tmp[0].val='p';
		long_options_tmp[1].name="ib-dev";long_options_tmp[1].has_arg=1;long_options_tmp[1].val='d';
		long_options_tmp[2].name="ib-port";long_options_tmp[2].has_arg=1;long_options_tmp[2].val='i';
		long_options_tmp[3].name="gid-idx";long_options_tmp[3].has_arg=1;long_options_tmp[3].val='g';
		long_options_tmp[4].name="service-level";long_options_tmp[4].has_arg=1;long_options_tmp[4].val='s';
		long_options_tmp[5].name="desired-rate";long_options_tmp[5].has_arg=1;long_options_tmp[5].val='r';
		long_options_tmp[6].name="help";long_options_tmp[6].has_arg=0;long_options_tmp[6].val='\0';
		long_options_tmp[7].name="NULL";long_options_tmp[7].has_arg=0;long_options_tmp[7].val='\0';
		/*
		static struct option long_options[] = {
			{.name = "port", .has_arg = 1, .val = 'p'},
			{.name = "ib-dev", .has_arg = 1, .val = 'd'},
			{.name = "ib-port", .has_arg = 1, .val = 'i'},
			{.name = "gid-idx", .has_arg = 1, .val = 'g'},
			{.name = "service-level", .has_arg = 1, .val = 's'},
			{.name = "help", .has_arg = 0, .val = 'h'},
			{.name = NULL, .has_arg = 0, .val = '\0'}
                };
		*/
		c = getopt_long(argc, argv, "p:d:i:g:s:h:", long_options_tmp, NULL);
		if (c == -1)
			break;
		switch (c)
		{
		case 'p':
			server_config.tcp_port = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			server_config.dev_name = strdup(optarg);
			break;
		case 'i':
			server_config.ib_port = strtoul(optarg, NULL, 0);
			if (server_config.ib_port < 0)
			{
				usage(argv[0]);
				return 1;
			}
			break;
		case 'g':
			server_config.gid_idx = strtoul(optarg, NULL, 0);
			if (server_config.gid_idx < 0)
			{
				usage(argv[0]);
				return 1;
			}
			break;
		case 's':
			server_config.sl = strtoul(optarg, NULL, 0);
			if (server_config.sl < 0)
			{
				usage(argv[0]);
				return 1;
			}
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}
	/* parse the last parameter (if exists) as the server name */
	if (optind == argc - 1)
		server_config.server_name = argv[optind];
	/* init all of the resources, so cleanup will be easy */
	resources_init(&res);
	/* create resources before using them */
	if (resources_create(&res, server_config))
	{
		fprintf(stderr, "failed to create resources\n");
	        if (resources_destroy(&res))
	        {
		        fprintf(stderr, "failed to destroy resources\n");
		        rc = 1;
	        }
	        if (server_config.dev_name)
		        free((char *)server_config.dev_name);
	        fprintf(stdout, "\ntest result is %d\n", rc);
	        return rc;
	}
	/* connect the QPs */
	
	if (connect_qp(&res, server_config))
	{
		fprintf(stderr, "failed to connect QPs\n");
	        if (resources_destroy(&res))
	        {
		        fprintf(stderr, "failed to destroy resources\n");
		        rc = 1;
	        }
	        if (server_config.dev_name)
		        free((char *)server_config.dev_name);
	        fprintf(stdout, "\ntest result is %d\n", rc);
	        return rc;
	}
	printf("Connected.\n");
        //handle_recv(&res);
	
	pthread_t threadIds[NUM_THREADS];
	struct resources res_copy[NUM_THREADS];
	for (int i=0; i<NUM_THREADS; i++) {
	    memcpy(&res_copy[i], &res, sizeof(struct resources));
	    res_copy[i].threadId = i;
	    pthread_create(&threadIds[i], NULL, process_per_thread, &res_copy[i]);
	}
	for (int i=0; i<NUM_THREADS; i++) {
	    pthread_join(threadIds[i], NULL);
	}
	rc = 0;
	if (resources_destroy(&res))
	{
		fprintf(stderr, "failed to destroy resources\n");
		rc = 1;
	}
	if (server_config.dev_name)
		free((char *)server_config.dev_name);
	fprintf(stdout, "\ntest result is %d\n", rc);
	
	return rc;
}
