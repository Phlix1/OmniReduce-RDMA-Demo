#include "common.h"
bool shutdown_flag = false;
int thread_count=NUM_THREADS+1;
bool flag=false;
std::mutex mutex;
std::condition_variable condition_variable;
struct config_t client_config = {
	NULL,  /* dev_name */
	NULL,  /* server_name */
	0,     /* number of servers or clients*/
	{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	19875, /* tcp_port */
	1,	 /* ib_port */
	-1, /* gid_idx */
	0   /* service level */
};
uint32_t find_next_nonzero_block(struct resources *res, uint32_t next_offset)
{
    uint32_t next_nonzero_offset = next_offset;
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    int slot = (next_nonzero_offset/MESSAGE_SIZE)%NUM_SLOTS;
    uint32_t max_index = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+slot*MESSAGE_SIZE;
    while (next_nonzero_offset-start_offset<DATA_SIZE_PER_THREAD) {
        if (res->bitmap[next_nonzero_offset/MESSAGE_SIZE]==1)
	    return next_nonzero_offset;
	next_nonzero_offset += MESSAGE_SIZE*NUM_SLOTS;
    }
    return max_index;
}
void handle_recv(struct resources *res, int slots)
{
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
                        uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
			fprintf(stdout, "threadId: %d; next index: %d; qp_num:%u\n", res->threadId, imm_data, wc[i].qp_num);
			std::cout<<"receiving: ";
                        for (int k=0; k<DATA_SIZE_PER_THREAD ;k++)
			    std::cout<<res->buf[k+start_offset]<<", ";
			std::cout<<std::endl;
#endif
			if (imm_data<max_index[0])
			{
			    int slot = (imm_data/MESSAGE_SIZE)%NUM_SLOTS;
			    if (res->bitmap[imm_data/MESSAGE_SIZE]==1) {
			        uint32_t next_offset = 0;
			        post_receive_client(res, slot+NUM_SLOTS*res->threadId, wc[i].qp_num);
			        //if(imm_data+MESSAGE_SIZE*NUM_SLOTS-start_offset>=DATA_SIZE_PER_THREAD)
			        //    next_offset = max_index[slot];
			        //else
			        //    next_offset = imm_data+MESSAGE_SIZE*NUM_SLOTS;
			        next_offset = find_next_nonzero_block(res, imm_data+MESSAGE_SIZE*NUM_SLOTS);
#ifdef DEBUG
			        std::cout<<"sending: ";
			        for (int k=0; k<DATA_SIZE_PER_THREAD ;k++)
			            std::cout<<res->buf[k+start_offset]<<", ";
			        std::cout<<std::endl;
#endif
	                        ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, imm_data, next_offset, slot+NUM_SLOTS*res->threadId, wc[i].qp_num);
			        if (ret)
	                        {
		                    fprintf(stderr, "failed to post SR\n");
				    exit(1);
	                        }
			    }
			    else {
			        post_receive_client(res, slot+NUM_SLOTS*res->threadId, wc[i].qp_num);
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
    //uint32_t start_bitmap_offset = BITMAP_SIZE_PER_THREAD*res->threadId;
    //uint32_t * next_offset = (uint32_t *)malloc(sizeof(uint32_t)*NUM_SLOTS);
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
            post_receive_client(res, i+NUM_SLOTS*res->threadId, 0);    	    
            //if (i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS>=DATA_SIZE_PER_THREAD)
	    //    next_offset[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
            //else
	    //    next_offset[i] = start_offset+i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS;
            uint32_t next_offset = find_next_nonzero_block(res, start_offset+i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS);
	    ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, start_offset+i*MESSAGE_SIZE, next_offset, i+NUM_SLOTS*res->threadId, 0);
            if (ret)
            {
	        fprintf(stderr, "failed to post SR\n");
	        exit(1);
		return NULL;
            }
        }
        handle_recv(res, first_burst);
	wait();
    }
    return NULL;
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
#ifdef DEBUG
	int num_processors = sysconf(_SC_NPROCESSORS_ONLN);
        std::cout<<"Number of processors: "<<num_processors<<std::endl;	
#endif
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
	client_config.isServer = false;
	/* parse the last parameter (if exists) as the server name */
	if (optind == argc - 1)
		client_config.server_name = argv[optind];
        char *token;
	const char s[2] = ",";
	token = strtok(client_config.server_name, s);
	while( token != NULL ) {
            client_config.peer_names[client_config.num_peers] = token;
	    client_config.num_peers++;
	    //printf( " %s\n", token);
	    token = strtok(NULL, s);
	}
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
	std::cout<<"Number of aggregators: "<<res.num_socks<<"; Number of workers is "<<res.num_machines<<"; My ID is "<<res.myId<<std::endl;
	printf("Connected.\n");
	
	// begin to send data 
	int warmups = 10;
	int num_rounds = 100;
        int round = 0;
	struct timeval cur_time;
        unsigned long start_time_msec;
        unsigned long diff_time_msec;
	unsigned long avg_time_msec=0;
	pthread_t threadIds[NUM_THREADS];

	pthread_attr_t attr;
	cpu_set_t cpus;
	pthread_attr_init(&attr);

	struct resources res_copy[NUM_THREADS];
	for (int i=0; i<NUM_THREADS; i++) {
	    CPU_ZERO(&cpus);
	    CPU_SET(i+8, &cpus);
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
	    memcpy(&res_copy[i], &res, sizeof(struct resources));
	    res_copy[i].threadId = i;
	    pthread_create(&threadIds[i], &attr, process_per_thread, &res_copy[i]);
	}
#ifdef DEBUG
	std::cout<<"Worker Id : "<<res.myId<<std::endl;
#endif
	srand(res.myId+1);
	double density_ratio = 1.0;
	double rnum = 0;
	while(round<num_rounds+warmups){
	    for (int i = 0; i< DATA_SIZE; i++)
	        res.buf[i] = 0;
	    for (int i = 0; i< BITMAP_SIZE; i++){
		rnum = rand()%100/(double)101;
		if (rnum < density_ratio)
	            res.bitmap[i]=1;
		else
		    res.bitmap[i]=0;
		if (res.bitmap[i]==1) {
		    for (int j=0; j<MESSAGE_SIZE; j++)
		        res.buf[i*MESSAGE_SIZE+j] = 0.01;
		}
	    }
#ifdef DEBUG
	    std::cout<<"AllReduce Input data worker "<<res.myId<<" : "<<std::endl;
	    for (int i = 0; i< DATA_SIZE; i++)
	        std::cout<<res.buf[i]<<" , ";
	    std::cout<<std::endl;
	    std::cout<<"**********\n";
#endif
	    gettimeofday(&cur_time, NULL);
	    wait();
	    wait();
#ifdef DEBUG
	    std::cout<<"AllReduce Output data worker "<<res.myId<<" : "<<std::endl;
	    for (int i = 0; i< DATA_SIZE; i++)
	        std::cout<<res.buf[i]<<" , ";
	    std::cout<<std::endl;
	    std::cout<<"**********\n";
#endif
	    start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	    gettimeofday(&cur_time, NULL);
	    diff_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000) - start_time_msec;
	    if (round>=warmups){
	        avg_time_msec += diff_time_msec;
	    }
	    fprintf(stdout, "data size: %ld Bytes; time: %ld ms; alg bw: %f GB/s\n", DATA_SIZE*sizeof(DATA_TYPE) ,diff_time_msec,(DATA_SIZE)*sizeof(DATA_TYPE)*1.0/1000000/diff_time_msec);
	    round++;
	}
	fprintf(stdout, "data size: %ld Bytes; average time: %ld ms; average alg bw: %f GB/s\n", DATA_SIZE*sizeof(DATA_TYPE) ,avg_time_msec/num_rounds,(DATA_SIZE)*sizeof(DATA_TYPE)*1.0/1000000/((float)avg_time_msec/num_rounds));
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
