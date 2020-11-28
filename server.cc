#include "common.h"

struct config_t server_config = {
	NULL,  /* dev_name */
	NULL,  /* server_name */
	0,     /* number of servers or clients*/
	{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	19875, /* tcp_port */
	1,	 /* ib_port */
	-1, /* gid_idx */
	0   /* service level */
};
void handle_recv(struct resources *res)
{
    uint32_t start_offset = DATA_SIZE_PER_THREAD*res->threadId;
    uint32_t max_index = (UINT32_MAX/BLOCK_SIZE/NUM_BLOCKS-1)*NUM_BLOCKS*BLOCK_SIZE;
    struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
    uint32_t current_offset[NUM_BLOCKS];
    uint32_t block_next_offset[NUM_BLOCKS][res->num_socks];
    int finished_blocks[NUM_SLOTS];
    int completed_blocks[NUM_SLOTS];
    uint32_t * current_offsets[NUM_SLOTS];
    uint32_t * next_offsets[NUM_SLOTS];
    uint32_t min_next_offset[NUM_BLOCKS];
    int register_count[NUM_SLOTS];
    uint32_t slot_to_qps[NUM_SLOTS][res->num_socks];
    int set[NUM_SLOTS];
    uint32_t buff_index[NUM_SLOTS];
    for (int i=0; i<NUM_SLOTS; i++) {
	register_count[i] = 0;
	set[i] = 0;
	min_next_offset[i] = 0;
	finished_blocks[i] = 0;
	completed_blocks[i] = 0;
	buff_index[i] = 0;
	current_offsets[i] = (uint32_t *)malloc(sizeof(uint32_t)*MESSAGE_SIZE);
	memset(current_offsets[i], 0, MESSAGE_SIZE*sizeof(uint32_t));
	next_offsets[i] = (uint32_t *)malloc(sizeof(uint32_t)*MESSAGE_SIZE);
	memset(next_offsets[i], 0, MESSAGE_SIZE*sizeof(uint32_t));
    }
    for(int w=0; w<res->num_socks; w++)
    {
        for(int i=0; i<NUM_BLOCKS; i++){
	    block_next_offset[i][w] = 0;
        }
    }
    for(int i=0; i<NUM_BLOCKS; i++)
	current_offset[i] = start_offset+i*BLOCK_SIZE;
    int first_burst = NUM_SLOTS*res->num_socks/res->num_machines;
    for(int i=0; i<first_burst; i++) {
        post_receive_server(res, i%(NUM_QPS*res->num_socks)+NUM_QPS*res->num_socks*res->threadId, 0);
//#ifdef STATISTICS
//	increment_receive(res->threadId);
//#endif
    }
    while (1) {
        int ne = ibv_poll_cq(res->cq[res->threadId], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
	if (ne>0){
//#ifdef STATISTICS
//	show_stat();
//#endif
            for (int i = 0; i < ne; ++i)
	    {
	        if (wc[i].status == IBV_WC_SUCCESS)	
		{
		    if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
#ifdef STATISTICS
	                increment_receive(res->threadId);
#endif
	                uint32_t block_num = wc[i].imm_data >> 16;
			int ret = 0;
			int slot = (wc[i].imm_data & 0x0000FFFF)%NUM_SLOTS;
			int global_slot = slot+NUM_SLOTS*res->threadId;
			int wid = get_workerid_by_qp_num(wc[i].qp_num);
			if(register_count[slot]<res->num_socks){
			    slot_to_qps[slot][wid] = wc[i].qp_num;
#ifdef DEBUG
			    fprintf(stdout, "QP : %u is registered in Slot %d\n", wc[i].qp_num, slot);
#endif
			    register_count[slot]++;
			}
			uint32_t * meta_ptr = (uint32_t *)(res->comm_buf+wid*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+BLOCK_SIZE*block_num+slot*(2*MESSAGE_SIZE)+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS);
			for(uint32_t k=0; k<block_num; k++) {
			    uint32_t block_offset = meta_ptr[k];
			    int bid = (block_offset/BLOCK_SIZE)%NUM_BLOCKS;
			    block_next_offset[bid][wid] = block_offset;
			    min_next_offset[bid] = block_next_offset[bid][0];
			    for(int j=1; j<res->num_socks; j++){
			        if(min_next_offset[bid] > block_next_offset[bid][j])
				    min_next_offset[bid] = block_next_offset[bid][j]; 
			    }
			    if(current_offset[bid]<min_next_offset[bid]){
				current_offsets[slot][completed_blocks[slot]] = current_offset[bid];
				next_offsets[slot][completed_blocks[slot]] = min_next_offset[bid];
				completed_blocks[slot]++;
			    }
			    for(int j=0; j<BLOCK_SIZE; j++)
			        res->comm_buf[(res->num_socks+COMM_BUFF_NUM)*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+set[slot]*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+(bid+res->threadId*NUM_BLOCKS)*BLOCK_SIZE+j] += res->comm_buf[wid*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS+slot*(2*MESSAGE_SIZE)+k*BLOCK_SIZE+j];
			}
			//fprintf(stdout, "threadid: %d; slot: %d; qp_num:%u; completed blocks: %d; finished blocks: %d\n", res->threadId, slot, wc[i].qp_num, completed_blocks[slot], finished_blocks[slot]);
#ifdef DEBUG
			fprintf(stdout, "threadid: %d; slot: %d; qp_num:%u; completed blocks: %d; finished blocks: %d\n", res->threadId, slot, wc[i].qp_num, completed_blocks[slot], finished_blocks[slot]);
			for(uint32_t k=0; k<block_num; k++) {
			    uint32_t block_offset = meta_ptr[k];
			    int bid = (block_offset/BLOCK_SIZE)%NUM_BLOCKS;
			    std::cout<<"block id: "<<bid<<"; value: ";
			    for(int j=0; j<BLOCK_SIZE; j++)
			        std::cout<<res->comm_buf[wid*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS+slot*(2*MESSAGE_SIZE)+k*BLOCK_SIZE+j]<<", ";
		            std::cout<<std::endl;    
			}
			std::cout<<"after receiving from "<<wid<<std::endl;
                        std::cout<<"##### next offset of block (slot="<<slot<<") #####\n";
			for(int w=0; w<res->num_socks; w++){
			    std::cout<<"worker "<<w<<" : "<<std::endl;
			    for(int j=0; j<BLOCKS_PER_MESSAGE; j++)
			        std::cout<<"block id: "<<slot*BLOCKS_PER_MESSAGE+j<<" -> "<<block_next_offset[slot*BLOCKS_PER_MESSAGE+j][w]<<std::endl;
			}
                        std::cout<<"##### aggregator state #####\n";
			for(int w=0; w<res->num_socks; w++){
			    std::cout<<"worker "<<w<<std::endl;
			    uint32_t ws = w*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+res->threadId*(2*MESSAGE_SIZE)*NUM_SLOTS;
		 	    for(int k=0; k<NUM_SLOTS; k++){
				std::cout<<"--slot "<<k<<std::endl;
				uint32_t ss = ws+slot*(2*MESSAGE_SIZE);
				uint32_t * t= (uint32_t*)(res->comm_buf+ss);
				for(int j=0; j<2*MESSAGE_SIZE; j++)
				    std::cout<<t[j]<<",";
			        std::cout<<std::endl;
			    }
			    std::cout<<std::endl;
			}
			for(int s=0; s<2; s++){
			    std::cout<<"set "<<s<<std::endl;
			    uint32_t as = (res->num_socks+1)*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+s*MESSAGE_SIZE*NUM_SLOTS*NUM_THREADS;

		 	    for(int k=0; k<MESSAGE_SIZE*NUM_SLOTS*NUM_THREADS; k++){
			        std::cout<<res->comm_buf[as+k]<<",";
			    }
			    std::cout<<std::endl;
			}
			std::cout<<"##########\n";
#endif
			if(completed_blocks[slot] >= BLOCKS_PER_MESSAGE-finished_blocks[slot]){
			    DATA_TYPE *tmp = res->comm_buf+NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS*(res->num_socks+buff_index[slot])+global_slot*(2*MESSAGE_SIZE);
			    for (int k=0; k<completed_blocks[slot]; k++)
			        memcpy(tmp+k*BLOCK_SIZE, res->comm_buf+NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS*(res->num_socks+COMM_BUFF_NUM)+NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS*set[slot]+(((current_offsets[slot][k]/BLOCK_SIZE)%NUM_BLOCKS)+res->threadId*NUM_BLOCKS)*BLOCK_SIZE, BLOCK_SIZE*sizeof(DATA_TYPE));
			    memcpy(tmp+BLOCK_SIZE*completed_blocks[slot], next_offsets[slot], completed_blocks[slot]*sizeof(uint32_t));
			    for(int k=global_slot*MESSAGE_SIZE; k<global_slot*MESSAGE_SIZE+MESSAGE_SIZE; k++){
			        res->comm_buf[(res->num_socks+COMM_BUFF_NUM)*NUM_SLOTS*(2*MESSAGE_SIZE)*NUM_THREADS+((set[slot]+1)%2)*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+k] = 0.0;
			    }
			    for(int k=0; k<res->num_socks; k++){
				for(int j=0; j<completed_blocks[slot] ;j++){
				    if (block_next_offset[(next_offsets[slot][j]/BLOCK_SIZE)%NUM_BLOCKS][k]==next_offsets[slot][j]) {
			                post_receive_server(res, global_slot, slot_to_qps[slot][k]);
					//std::cout<<"post receve to "<<k<<std::endl;
//#ifdef STATISTICS
//				        increment_receive(res->threadId);
//#endif
					break;
				    }
				}
			        ret = post_send_server(res, IBV_WR_RDMA_WRITE_WITH_IMM, completed_blocks[slot], current_offsets[slot], next_offsets[slot], global_slot, slot_to_qps[slot][k], set[slot], buff_index[slot]);
#ifdef STATISTICS
				increment_send(res->threadId);
#endif
			        if(ret)
			        {
			            fprintf(stderr, "failed to post SR\n");
		                    exit(1);
			        }
			    }
			    buff_index[slot] = (buff_index[slot]+1)%COMM_BUFF_NUM;
			    for(int j=0; j<completed_blocks[slot] ;j++){
				current_offset[(next_offsets[slot][j]/BLOCK_SIZE)%NUM_BLOCKS] = next_offsets[slot][j];
				if (next_offsets[slot][j]>=max_index) {
				    current_offset[(next_offsets[slot][j]/BLOCK_SIZE)%NUM_BLOCKS] = start_offset+((next_offsets[slot][j]/BLOCK_SIZE)%NUM_BLOCKS)*BLOCK_SIZE;
			            for(int k=0; k<res->num_socks; k++){
				        block_next_offset[(next_offsets[slot][j]/BLOCK_SIZE)%NUM_BLOCKS][k] = 0; 
			            }
				    finished_blocks[slot]++;
				}
			    }
			    completed_blocks[slot] = 0;
			    if (finished_blocks[slot]==BLOCKS_PER_MESSAGE) {
				finished_blocks[slot] = 0;
				buff_index[slot] = 0;
			        for(int k=0; k<res->num_socks; k++)
			            post_receive_server(res, global_slot, slot_to_qps[slot][k]);
#ifdef STATISTICS
	                        show_stat();
#endif
			    }
			    set[slot] = (set[slot]+1)%2;
			}
		    }												                        }
	    }
	}
    }
}
void *process_per_thread(void *arg)
{
    struct resources *res = (struct resources *)arg;
    handle_recv(res);
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
	server_config.isServer = true;
	/* parse the last parameter (if exists) as the server name */
	if (optind == argc - 1)
		server_config.server_name = argv[optind];
	char *token;
	const char s[2] = ",";
	token = strtok(server_config.server_name, s);
	while( token != NULL ) {
	    server_config.peer_names[server_config.num_peers] = token;
	    server_config.num_peers++;
	    //printf( " %s\n", token );	     
	    token = strtok(NULL, s);
	}
	/* print the used parameters for info*/
	print_config(server_config);
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
        std::cout<<"Number of aggregators: "<<res.num_machines<<"; Number of workers is "<<res.num_socks<<"; My ID is "<<res.myId<<std::endl;
	printf("Connected.\n");
	
	pthread_t threadIds[NUM_THREADS];
	struct resources res_copy[NUM_THREADS];
	pthread_attr_t attr;
	cpu_set_t cpus;
	pthread_attr_init(&attr);

	for (int i=0; i<NUM_THREADS; i++) {
	    CPU_ZERO(&cpus);
	    CPU_SET(i+8, &cpus);
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
	    memcpy(&res_copy[i], &res, sizeof(struct resources));
	    res_copy[i].threadId = i;
	    pthread_create(&threadIds[i], &attr, process_per_thread, &res_copy[i]);
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
