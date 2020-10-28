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
    uint32_t max_index = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE;
    uint32_t next_offset = 0;
    struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
    uint32_t current_offset[NUM_SLOTS];
    uint32_t slot_next_offset[NUM_SLOTS][res->num_socks];
    uint32_t min_next_offset[NUM_SLOTS];
    int register_count[NUM_SLOTS];
    uint32_t slot_to_qps[NUM_SLOTS][res->num_socks];
    int set[NUM_SLOTS];
    for (int i=0; i<NUM_SLOTS; i++) {
	register_count[i] = 0;
	set[i] = 0;
	min_next_offset[i] = 0;
    }
    for(int w=0; w<res->num_socks; w++)
    {
        for(int i=0; i<NUM_SLOTS; i++){
	    current_offset[i] = start_offset+i*MESSAGE_SIZE;
	    slot_next_offset[i][w] = 0;
        }
    }
    int first_burst = NUM_SLOTS*res->num_socks/res->num_machines;
    for(int i=0; i<first_burst; i++) {
        post_receive_server(res, i%(NUM_QPS*res->num_socks)+NUM_QPS*res->num_socks*res->threadId, 0);
#ifdef STATISTICS
	increment_receive(res->threadId);
#endif
    }
    while (1) {
        int ne = ibv_poll_cq(res->cq[res->threadId], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
	if (ne>0){
#ifdef STATISTICS
	show_stat();
#endif
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
			int global_slot = slot+NUM_SLOTS*res->threadId;
			int wid = get_workerid_by_qp_num(wc[i].qp_num);
			if(register_count[slot]<res->num_socks){
			    slot_to_qps[slot][wid] = wc[i].qp_num;
#ifdef DEBUG
			    fprintf(stdout, "QP : %u is registered in Slot %d\n", wc[i].qp_num, slot);
#endif
			    register_count[slot]++;
			}
			for(int k=global_slot*MESSAGE_SIZE; k<global_slot*MESSAGE_SIZE+MESSAGE_SIZE; k++){
			    res->buf[(res->num_socks+set[slot])*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+k] += res->buf[wid*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+k];
			}
			slot_next_offset[slot][wid] = wc[i].imm_data;
			min_next_offset[slot] = slot_next_offset[slot][0];
			for(int k=1; k<res->num_socks; k++){
			    if(min_next_offset[slot] > slot_next_offset[slot][k])
				min_next_offset[slot] = slot_next_offset[slot][k]; 
			}
#ifdef DEBUG
			fprintf(stdout, "next block: %d; slot: %d; qp_num:%u\n", next_offset/MESSAGE_SIZE, (next_offset/MESSAGE_SIZE)%NUM_SLOTS, wc[i].qp_num);
			fprintf(stdout, "threadid: %d; slot: %d; current offset: %d; nextoffset: %d; message size:%d; num slots:%d, min next offset:%d\n", res->threadId, slot+NUM_SLOTS*res->threadId, current_offset[slot], next_offset, MESSAGE_SIZE, NUM_SLOTS, min_next_offset[slot]);
			std::cout<<"after receiving from "<<wid<<std::endl;
                        std::cout<<"##### next offset of slot"<<slot<<" #####\n";
			for(int w=0; w<res->num_socks; w++){
			    std::cout<<"worker "<<w<<" : "<<slot_next_offset[slot][w]<<std::endl;
			}
                        std::cout<<"##### aggregator state #####\n";
			for(int w=0; w<res->num_socks; w++){
			    std::cout<<"worker "<<w<<std::endl;
		 	    for(int k=0; k<MESSAGE_SIZE*NUM_SLOTS*NUM_THREADS; k++){
			        std::cout<<res->buf[w*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+k]<<",";
			    }
			    std::cout<<std::endl;
			}
			for(int s=0; s<2; s++){
			    std::cout<<"set "<<s<<std::endl;
		 	    for(int k=0; k<MESSAGE_SIZE*NUM_SLOTS*NUM_THREADS; k++){
			        std::cout<<res->buf[(s+res->num_socks)*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+k]<<",";
			    }
			    std::cout<<std::endl;
			}
			std::cout<<"##########\n";
#endif
			if(current_offset[slot]<min_next_offset[slot]){
			    for(int k=global_slot*MESSAGE_SIZE; k<global_slot*MESSAGE_SIZE+MESSAGE_SIZE; k++){
			        res->buf[(res->num_socks+(set[slot]+1)%2)*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+k] = 0.0;
			    }
			    for(int k=0; k<res->num_socks; k++){
				if (min_next_offset[slot]==slot_next_offset[slot][k]){
			            post_receive_server(res, global_slot, slot_to_qps[slot][k]);
#ifdef STATISTICS
				    increment_receive(res->threadId);
#endif
				}
			        ret = post_send_server(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, current_offset[slot], min_next_offset[slot], global_slot, slot_to_qps[slot][k], set[slot]);
#ifdef STATISTICS
				increment_send(res->threadId);
#endif
			        if(ret)
			        {
			            fprintf(stderr, "failed to post SR\n");
		                    exit(1);
			        }
			    }
			    if (min_next_offset[slot]<max_index) {
			        current_offset[slot] = min_next_offset[slot];
			    }
			    else {
			        current_offset[slot] = start_offset+slot*MESSAGE_SIZE;
			        for(int k=0; k<res->num_socks; k++){
				    slot_next_offset[slot][k] = 0; 
			        }
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
