#include "common.h"
//#include "mpi.h"
//#define CHECK
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
    int bid = (next_nonzero_offset/BLOCK_SIZE)%NUM_BLOCKS;
    uint32_t max_index = (UINT32_MAX/BLOCK_SIZE/NUM_BLOCKS-1)*NUM_BLOCKS*BLOCK_SIZE+bid*BLOCK_SIZE;
    while (next_nonzero_offset-start_offset<DATA_SIZE_PER_THREAD) {
        if (res->bitmap[next_nonzero_offset/BLOCK_SIZE]==1)
	    return next_nonzero_offset;
	next_nonzero_offset += BLOCK_SIZE*NUM_BLOCKS;
    }
    return max_index;
}
void handle_recv(struct resources *res, int slots)
{
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    uint32_t max_index[NUM_BLOCKS];
    uint32_t current_offset[NUM_BLOCKS];
    int finished_blocks[NUM_SLOTS];
    uint32_t buff_index[NUM_SLOTS];
    uint32_t * current_offsets = (uint32_t *)malloc(sizeof(uint32_t)*MESSAGE_SIZE);
    uint32_t * next_offsets = (uint32_t *)malloc(sizeof(uint32_t)*MESSAGE_SIZE);
    for(int i=0; i<NUM_BLOCKS; i++){
        max_index[i] = (UINT32_MAX/BLOCK_SIZE/NUM_BLOCKS-1)*NUM_BLOCKS*BLOCK_SIZE+i*BLOCK_SIZE;
        current_offset[i] = start_offset+i*BLOCK_SIZE;
    }
    for(int i=0; i<NUM_SLOTS; i++){
        finished_blocks[i] = 0;
	buff_index[i] = 0;
    }
    int finished_slots = 0;
    int ret = 0;
    struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
    while (finished_slots<slots) {
        int ne = ibv_poll_cq(res->cq[res->threadId], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
	if (ne>0){
	    //std::cout<<"packet num: "<<ne<<std::endl;
	    for (int i = 0; i < ne; ++i)
	    {
	        if (wc[i].status == IBV_WC_SUCCESS)
		{
		    if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
#ifdef STATISTICS
			increment_receive(res->threadId);
#endif
			uint32_t block_num = wc[i].imm_data >> 16;
			int slot = (wc[i].imm_data & 0x0000FFFF)%NUM_SLOTS;
	                post_receive_client(res, slot+NUM_SLOTS*res->threadId, wc[i].qp_num);
			uint32_t * meta_ptr = (uint32_t *)(res->comm_buf+BLOCK_SIZE*block_num+slot*(2*MESSAGE_SIZE)+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS+buff_index[slot]*(2*MESSAGE_SIZE)*NUM_SLOTS*NUM_THREADS);
			//fprintf(stdout, "threadId: %d; block num: %d; slot: %d; qp_num:%u, next: %d\n", res->threadId, block_num, slot, wc[i].qp_num, meta_ptr[0]);
			//fprintf(stdout, "threadId: %d; block num: %d; slot: %d; qp_num:%u\n", res->threadId, block_num, slot, wc[i].qp_num);
			//std::cout<<"next offsets: ";
			//for (uint32_t k=0; k<block_num; k++)
			//    std::cout<<meta_ptr[k]<<", ";
			//std::cout<<std::endl;
			//fprintf(stdout, "threadId: %d; block num: %d; slot: %d; qp_num:%u\n", res->threadId, block_num, slot, wc[i].qp_num);
#ifdef DEBUG
			fprintf(stdout, "threadId: %d; block num: %d; slot: %d; qp_num:%u\n", res->threadId, block_num, slot, wc[i].qp_num);
			std::cout<<"receiving: ";
                        for (uint32_t k=slot*(2*MESSAGE_SIZE)+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS+buff_index[slot]*(2*MESSAGE_SIZE)*NUM_SLOTS*NUM_THREADS; k<BLOCK_SIZE*block_num+slot*(2*MESSAGE_SIZE)+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS+buff_index[slot]*(2*MESSAGE_SIZE)*NUM_SLOTS*NUM_THREADS; k++)
			    std::cout<<res->comm_buf[k]<<", ";
			std::cout<<"next offsets: ";
			for (uint32_t k=0; k<block_num; k++)
			    std::cout<<meta_ptr[k]<<", ";
			std::cout<<std::endl;
#endif
                        int nonzero_block_num = 0;
			//std::cout<<slot<<" buff index: "<<buff_index[slot]<<"; nexttsi : "<<meta_ptr[0]<<"; "<<res->comm_buf[BLOCK_SIZE*block_num+slot*(2*MESSAGE_SIZE)+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS+buff_index[slot]*(2*MESSAGE_SIZE)*NUM_SLOTS*NUM_THREADS]<<std::endl;
			for (uint32_t k=0; k<block_num; k++){
			    int bid = (meta_ptr[k]/BLOCK_SIZE)%NUM_BLOCKS;
                            memcpy(res->buf+current_offset[bid], res->comm_buf+(2*MESSAGE_SIZE)*(slot+NUM_SLOTS*res->threadId)+k*BLOCK_SIZE+buff_index[slot]*(2*MESSAGE_SIZE)*NUM_SLOTS*NUM_THREADS, BLOCK_SIZE*sizeof(DATA_TYPE));
			    current_offset[bid] = meta_ptr[k];
			    if (current_offset[bid]<max_index[0]) {
				if (res->bitmap[current_offset[bid]/BLOCK_SIZE]==1) {
				    current_offsets[nonzero_block_num] = current_offset[bid];
				    next_offsets[nonzero_block_num] = find_next_nonzero_block(res, current_offset[bid]+BLOCK_SIZE*NUM_BLOCKS);
				    nonzero_block_num++;
				}
			    }
			    else {
				current_offset[bid] = start_offset+bid*BLOCK_SIZE;
				finished_blocks[slot]++;
			    }
			}
			buff_index[slot] = (buff_index[slot]+1)%COMM_BUFF_NUM;
#ifdef DEBUG
			        std::cout<<"data after receiving: ";
			        for (int k=0; k<DATA_SIZE_PER_THREAD ;k++)
			            std::cout<<res->buf[k+start_offset]<<", ";
			        std::cout<<std::endl;
#endif
			if (finished_blocks[slot] < BLOCKS_PER_MESSAGE)
			{
			    if (nonzero_block_num>0) {
			        //post_receive_client(res, slot+NUM_SLOTS*res->threadId, wc[i].qp_num);
//#ifdef STATISTICS
//				increment_receive(res->threadId);
//#endif
			        //if(imm_data+MESSAGE_SIZE*NUM_SLOTS-start_offset>=DATA_SIZE_PER_THREAD)
			        //    next_offset = max_index[slot];
			        //else
			        //    next_offset = imm_data+MESSAGE_SIZE*NUM_SLOTS;
#ifdef DEBUG
			        std::cout<<"sending: ";
			        for (int k=0; k<DATA_SIZE_PER_THREAD ;k++)
			            std::cout<<res->buf[k+start_offset]<<", ";
			        std::cout<<std::endl;
#endif
	                        ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, nonzero_block_num, current_offsets, next_offsets, slot+NUM_SLOTS*res->threadId, wc[i].qp_num, buff_index[slot]);
#ifdef STATISTICS
			        increment_send(res->threadId);
#endif
				if (ret)
	                        {
		                    fprintf(stderr, "failed to post SR\n");
				    exit(1);
	                        }
			    }
			    else {
			        //post_receive_client(res, slot+NUM_SLOTS*res->threadId, wc[i].qp_num);
//#ifdef STATISTICS
//				increment_receive(res->threadId);
//#endif
			    }
			}
			else {
			    finished_slots++;
                        }
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
    //struct timeval cur_time;
    //unsigned long start_time_usec;
    struct resources *res = (struct resources *)arg;
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    //uint32_t start_bitmap_offset = BITMAP_SIZE_PER_THREAD*res->threadId;
    //uint32_t * next_offset = (uint32_t *)malloc(sizeof(uint32_t)*NUM_SLOTS);
    uint32_t * current_offsets = (uint32_t *)malloc(sizeof(uint32_t)*MESSAGE_SIZE);
    uint32_t * next_offsets = (uint32_t *)malloc(sizeof(uint32_t)*MESSAGE_SIZE);
    int ret = 0;
    int n_messages = DATA_SIZE_PER_THREAD/MESSAGE_SIZE;
    int first_burst = (n_messages < NUM_SLOTS) ? n_messages:NUM_SLOTS;
    for (int i=0; i<first_burst; i++)
        for(int j=0; j<PREPOST_NUM; j++)
            post_receive_client(res, i+NUM_SLOTS*res->threadId, 0);
    while (true) {
        wait();
	if (shutdown_flag) {
	    //std::cout << "Thread "<<res->threadId<<" shutting down.\n";
	    break;
	}
        //gettimeofday(&cur_time, NULL);	
        for (int i=0; i<first_burst; i++){
            //for(int j=0; j<PREPOST_NUM; j++)
            //    post_receive_client(res, i+NUM_SLOTS*res->threadId, 0);
//#ifdef STATISTICS
//	    increment_receive(res->threadId);
//#endif
            //if (i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS>=DATA_SIZE_PER_THREAD)
	    //    next_offset[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
            //else
	    //    next_offset[i] = start_offset+i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS;
	    for (int j=0; j<BLOCKS_PER_MESSAGE; j++) {
		current_offsets[j] = start_offset+i*MESSAGE_SIZE+j*BLOCK_SIZE;
		next_offsets[j] = find_next_nonzero_block(res, current_offsets[j]+BLOCK_SIZE*NUM_BLOCKS);
	    }
	    ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, BLOCKS_PER_MESSAGE, current_offsets, next_offsets, i+NUM_SLOTS*res->threadId, 0, 0);
#ifdef STATISTICS
	    increment_send(res->threadId);
#endif
            if (ret)
            {
	        fprintf(stderr, "failed to post SR\n");
	        exit(1);
		return NULL;
            }
        }
        handle_recv(res, first_burst);
	//start_time_usec = (cur_time.tv_sec * 1000000) + (cur_time.tv_usec);
	//gettimeofday(&cur_time, NULL);
	//std::cout<<res->threadId<<":"<<(cur_time.tv_sec * 1000000) + (cur_time.tv_usec) - start_time_usec<<std::endl;
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
        //MPI_Init(&argc, &argv);
        int myrank=0, worldsize=1;
        //MPI_Comm_size(MPI_COMM_WORLD, &worldsize);
        //MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
#ifdef DEBUG
        std::cout<<myrank<<" "<<worldsize<<std::endl;
	int num_processors = sysconf(_SC_NPROCESSORS_ONLN);
        std::cout<<"Number of processors: "<<num_processors<<std::endl;	
#endif
	struct resources res;
	int rc = 1;
        double density_ratio = 1.0;
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
		long_options_tmp[5].name="density-ratio";long_options_tmp[5].has_arg=1;long_options_tmp[5].val='r';
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
			density_ratio = strtod(optarg, NULL);
			if (density_ratio < 0)
			{
				density_ratio = 1.0;
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
	int num_rounds = 101;
	int print_freq = 1;
	int print_count = 0;
        int round = 0;
	struct timeval cur_time;
        unsigned long start_time_usec;
        unsigned long diff_time_usec;
	unsigned long avg_time_usec=0;
	double avg_bw = 0.0;
	pthread_t threadIds[NUM_THREADS];

	pthread_attr_t attr;
	cpu_set_t cpus;
	pthread_attr_init(&attr);

	struct resources res_copy[NUM_THREADS];
	for (int i=0; i<NUM_THREADS; i++) {
	    CPU_ZERO(&cpus);
	    CPU_SET(i+10, &cpus);
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
	    memcpy(&res_copy[i], &res, sizeof(struct resources));
	    res_copy[i].threadId = i;
	    pthread_create(&threadIds[i], &attr, process_per_thread, &res_copy[i]);
	}
#ifdef DEBUG
	std::cout<<"Worker Id : "<<res.myId<<std::endl;
#endif
	srand(res.myId+1);
	double rnum = 0;
	int non_zero_count = 0;
        DATA_TYPE *input = (DATA_TYPE *)malloc(DATA_SIZE*sizeof(DATA_TYPE));
        DATA_TYPE *output = (DATA_TYPE *)malloc(DATA_SIZE*sizeof(DATA_TYPE));
	for (int i = 0; i< DATA_SIZE; i++) {
	    res.buf[i] = 0;
            input[i] = 0;
        }
        std::cout<<"density: "<<density_ratio<<std::endl;
	for (int i = 0; i< BITMAP_SIZE; i++){
	    rnum = rand()%100/(double)101;
	    if (rnum < density_ratio && res.myId!=-1){
	        res.bitmap[i]=1;
		non_zero_count++;
	    }
            else {
		res.bitmap[i]=0;
	    }
	    if (res.bitmap[i]==1) {
		for (int j=0; j<BLOCK_SIZE; j++){
		    res.buf[i*BLOCK_SIZE+j] = 0.01;
                    input[i*BLOCK_SIZE+j] = 0.01;
                }
	    }
	}
	while(round<num_rounds+warmups){
#ifdef DEBUG
	    std::cout<<"AllReduce Input data worker "<<res.myId<<" : "<<std::endl;
	    for (int i = 0; i< DATA_SIZE; i++)
	        std::cout<<res.buf[i]<<" , ";
	    std::cout<<std::endl;
	    std::cout<<"**********\n";
#endif
	    wait();
	    wait();
#ifdef DEBUG
	    std::cout<<"AllReduce Output data worker "<<res.myId<<" : "<<std::endl;
	    for (int i = 0; i< DATA_SIZE; i++)
	        std::cout<<res.buf[i]<<" , ";
	    std::cout<<std::endl;
	    std::cout<<"**********\n";
#endif
	    if (round>=warmups and (round-warmups)%print_freq==0){
		if ((round-warmups)/print_freq>0){
	            gettimeofday(&cur_time, NULL);
	            diff_time_usec = (cur_time.tv_sec * 1000000) + (cur_time.tv_usec) - start_time_usec;
		    print_count ++;
	            avg_time_usec += diff_time_usec/print_freq;
		    avg_bw += print_freq*(DATA_SIZE)*sizeof(DATA_TYPE)*1.0/(1024*1024*1024)/((double)diff_time_usec/1000000);
                    if(myrank==0)
	                fprintf(stdout, "data size: %ld Bytes; time: %ld us; alg bw: %f GB/s\n", DATA_SIZE*sizeof(DATA_TYPE) ,diff_time_usec/print_freq, print_freq*(DATA_SIZE)*sizeof(DATA_TYPE)*1.0/(1024*1024*1024)/((double)diff_time_usec/1000000));
		}
#ifdef CHECK
                MPI_Allreduce(input, output, DATA_SIZE, MPI_FLOAT, MPI_SUM, MPI_COMM_WORLD);
                int r = 0;
		for(int i=0; i<DATA_SIZE; i++){
                    if(output[i]!=res.buf[i]){
		        std::cout<<"error: "<<output[i]<<"<---->"<<res.buf[i]<<std::endl;
                        r = 1;
                        break;
                    }
                }
                if(r)
                    exit(1);
                else
                    std::cout<<"check OK"<<std::endl;
                for(int i=0; i<DATA_SIZE; i++)
                    res.buf[i] = input[i];
#endif
                //MPI_Barrier(MPI_COMM_WORLD);
	        gettimeofday(&cur_time, NULL);
	        start_time_usec = (cur_time.tv_sec * 1000000) + (cur_time.tv_usec);
		//std::cout<<"round over: "<<round<<std::endl;
	    }
	    round++;
	}
	fprintf(stdout, "data size: %ld Bytes; average time: %ld us; average alg bw: %f GB/s\n", DATA_SIZE*sizeof(DATA_TYPE), avg_time_usec/print_count, avg_bw/print_count);
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
#ifdef STATISTICS
	show_stat();
#endif
	return rc;
}
