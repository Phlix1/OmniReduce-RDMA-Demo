#include "common.h"
bool shutdown_flag = false;
int thread_count=NUM_THREADS+1;
bool flag=false;
std::mutex mutex;
std::condition_variable condition_variable;
struct config_t client_config = {
	NULL,  /* dev_name */
	NULL,  /* server_name */
	19875, /* tcp_port */
	1,	 /* ib_port */
	-1, /* gid_idx */
	0   /* service level */
};

void handle_recv(struct resources *res, int slots)
{
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    uint32_t max_index[NUM_SLOTS];
    for(int i=0; i<NUM_SLOTS; i++){
        max_index[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
    }
    int finished_slots = 0;
    int ret = 0;
    struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
    while (finished_slots<slots) {
        int ne = ibv_poll_cq(res->cq[res->threadId], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
	if (ne>0){
	    for (int i = 0; i < ne; ++i)
	    {
	        if (wc[i].status == IBV_WC_SUCCESS)
		{
		    if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
			uint32_t imm_data = wc[i].imm_data;
#ifdef DEBUG
			fprintf(stdout, "threadId: %d; next index: %d\n", res->threadId, imm_data);
			fprintf(stdout, "receiving :%c, %c, %c, %c, %c, %c, %c, %c, %c, %c\n", res->buf[0], res->buf[1], res->buf[2], res->buf[3], res->buf[4], res->buf[5], res->buf[6], res->buf[7], res->buf[8], res->buf[9]);
#endif
			if (imm_data<max_index[0])
			{
			    int next_offset = 0;
			    int slot = (imm_data/MESSAGE_SIZE)%NUM_SLOTS;
			    post_receive_client(res, imm_data, slot+NUM_SLOTS*res->threadId);
			    if(imm_data+MESSAGE_SIZE*NUM_SLOTS-start_offset>=DATA_SIZE_PER_THREAD)
				next_offset = max_index[slot];
			    else
			        next_offset = imm_data+MESSAGE_SIZE*NUM_SLOTS;
#ifdef DEBUG
                            fprintf(stdout, "sending :%c, %c, %c, %c, %c, %c, %c, %c, %c, %c\n", res->buf[0], res->buf[1], res->buf[2], res->buf[3], res->buf[4], res->buf[5], res->buf[6], res->buf[7], res->buf[8], res->buf[9]);
#endif
	                    ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, imm_data, next_offset, slot+NUM_SLOTS*res->threadId);
			    if (ret)
	                    {
		                fprintf(stderr, "failed to post SR\n");
				exit(1);
	                    }
			}
			else
			    finished_slots++;
		    }
		}
	    }
	}
    }
}
void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    const bool flag_copy = flag;
    --thread_count;
    if (thread_count>0) {
        while (flag_copy == flag) {
	    condition_variable.wait(lock);
	}
    }
    else {
        flag = !flag;
	thread_count = NUM_THREADS+1;
	condition_variable.notify_all();
    }
}
void *process_per_thread(void *arg)
{
    struct resources *res = (struct resources *)arg;
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    uint32_t * next_offset = (uint32_t *)malloc(sizeof(uint32_t)*NUM_SLOTS);
    int ret = 0;
    int n_messages = DATA_SIZE_PER_THREAD/MESSAGE_SIZE;
    int first_burst = (n_messages < NUM_SLOTS) ? n_messages:NUM_SLOTS;
    while (true) {
        wait();
	if (shutdown_flag) {
	    std::cout << "Thread "<<res->threadId<<" shutting down.\n";
	    break;
	}	
        for (int i=0; i<first_burst; i++){
            post_receive_client(res, start_offset+i*MESSAGE_SIZE, i+NUM_SLOTS*res->threadId);    	    
            if (i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS>=DATA_SIZE_PER_THREAD)
	        next_offset[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
            else
	        next_offset[i] = start_offset+i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS;
            ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, start_offset+i*MESSAGE_SIZE, next_offset[i], i+NUM_SLOTS*res->threadId);
            if (ret)
            {
	        fprintf(stderr, "failed to post SR\n");
	        exit(1);
            }
        }
        handle_recv(res, first_burst);
	wait();
    }
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
	float desired_rate = 10.0; //unit: Gbps, the max value is 40G,otherwise MSG_SIZE is modified
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
			{.name = "desired-rate", .has_arg = 1, .val = 'r'},
			{.name = "help", .has_arg = 0, .val = '\0'},
			{.name = NULL, .has_arg = 0, .val = '\0'}
		
                };*/
		c = getopt_long(argc, argv, "p:d:i:g:s:r:h:", long_options_tmp, NULL);
		if (c == -1)
			break;
		switch (c)
		{
		case 'p':
			client_config.tcp_port = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			client_config.dev_name = strdup(optarg);
			break;
		case 'i':
			client_config.ib_port = strtoul(optarg, NULL, 0);
			if (client_config.ib_port < 0)
			{
				usage(argv[0]);
				return 1;
			}
			break;
		case 'g':
			client_config.gid_idx = strtoul(optarg, NULL, 0);
			if (client_config.gid_idx < 0)
			{
				usage(argv[0]);
				return 1;
			}
			break;
		case 's':
			client_config.sl = strtoul(optarg, NULL, 0);
			if (client_config.sl < 0)
			{
				usage(argv[0]);
				return 1;
			}
			break;
		case 'r':
			desired_rate = strtoul(optarg, NULL, 0);
			if (desired_rate == 0)
			{
				desired_rate = 10;
			}
			break;
		case 'h':
			usage(argv[0]);
			return 1;
		default:
			usage(argv[0]);
			return 1;
		}
	}
	/* parse the last parameter (if exists) as the server name */
	if (optind == argc - 1)
		client_config.server_name = argv[optind];
	/* print the used parameters for info*/
	print_config(client_config);
	/* init all of the resources, so cleanup will be easy */
	resources_init(&res);
	/* create resources before using them */
	if (resources_create(&res, client_config))
	{
		fprintf(stderr, "failed to create resources\n");
                if (resources_destroy(&res))
		{
	            fprintf(stderr, "failed to destroy resources\n");
		    rc = 1;
		}
	        if (client_config.dev_name)
	            free((char *)client_config.dev_name);
		fprintf(stdout, "\ntest result is %d\n", rc);
	        return rc;
	}
	fprintf(stdout, "start connected\n");
	/* connect the QPs */
	if (connect_qp(&res, client_config))
	{
		fprintf(stderr, "failed to connect QPs\n");
                if (resources_destroy(&res))
		{
	            fprintf(stderr, "failed to destroy resources\n");
		    rc = 1;
		}
	        if (client_config.dev_name)
	            free((char *)client_config.dev_name);
		fprintf(stdout, "\ntest result is %d\n", rc);
	        return rc;
	}
	printf("Connected.\n");
	// begin to send data 
	int num_rounds = 10;
        int round = 0;
	struct timeval cur_time;
        unsigned long start_time_msec;
        unsigned long diff_time_msec;
	pthread_t threadIds[NUM_THREADS];
	struct resources res_copy[NUM_THREADS];
	for (int i=0; i<NUM_THREADS; i++) {
	    memcpy(&res_copy[i], &res, sizeof(struct resources));
	    res_copy[i].threadId = i;
	    pthread_create(&threadIds[i], NULL, process_per_thread, &res_copy[i]);
	}
	while(round<num_rounds){
	    for (int i = 0; i< DATA_SIZE; i++)
	        res.buf[i] = 'a'+i%10;
	    gettimeofday(&cur_time, NULL);
	    //process_per_thread(&res);
	    //pthread_create(&threadId, NULL, process_per_thread, &res);
	    //pthread_join(threadId, NULL);
	    wait();
	    wait();
	    start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	    gettimeofday(&cur_time, NULL);
	    diff_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000) - start_time_msec;
	    fprintf(stdout, "data size: %d Bytes; time: %ld ms; thoughput: %f Gbps\n", DATA_SIZE ,diff_time_msec,(DATA_SIZE)*8.0/1000000/diff_time_msec);
	    round++;
	}
	shutdown_flag = true;
	wait();
	for (int i=0; i<NUM_THREADS; i++)
	    pthread_join(threadIds[i], NULL);
	rc = 0;
	if (resources_destroy(&res))
	{
		fprintf(stderr, "failed to destroy resources\n");
		rc = 1;
	}
	if (client_config.dev_name)
		free((char *)client_config.dev_name);
	fprintf(stdout, "\ntest result is %d\n", rc);
	return rc;
}
