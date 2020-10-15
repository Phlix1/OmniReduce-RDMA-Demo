#include "common.h"
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
    uint32_t max_index[NUM_SLOTS];
    for(int i=0; i<NUM_SLOTS; i++){
        max_index[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
    }
    int finished_slots = 0;
    int ret = 0;
    struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
    while (finished_slots<slots) {
        int ne = ibv_poll_cq(res->cq, MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
	if (ne>0){
	    for (int i = 0; i < ne; ++i)
	    {
	        if (wc[i].status == IBV_WC_SUCCESS)
		{
		    if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
			uint32_t imm_data = wc[i].imm_data;
#ifdef DEBUG
			fprintf(stdout, "immediate: %d\n", imm_data);
			fprintf(stdout, "receiving :%c, %c, %c, %c, %c, %c, %c, %c, %c, %c\n", res->buf[0], res->buf[1], res->buf[2], res->buf[3], res->buf[4], res->buf[5], res->buf[6], res->buf[7], res->buf[8], res->buf[9]);
#endif
			if (imm_data<max_index[0])
			{
			    int next_offset = 0;
			    int slot = (imm_data/MESSAGE_SIZE)%NUM_SLOTS;
			    post_receive_client(res, imm_data, slot);
			    if(imm_data+MESSAGE_SIZE*NUM_SLOTS>=DATA_SIZE)
				next_offset = max_index[slot];
			    else
			        next_offset = imm_data+MESSAGE_SIZE*NUM_SLOTS;
#ifdef DEBUG
                            fprintf(stdout, "sending :%c, %c, %c, %c, %c, %c, %c, %c, %c, %c\n", res->buf[0], res->buf[1], res->buf[2], res->buf[3], res->buf[4], res->buf[5], res->buf[6], res->buf[7], res->buf[8], res->buf[9]);
#endif
	                    ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, imm_data, next_offset, slot);
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

void process_per_thread(struct resources *res, uint32_t *next_offset)
{
    int ret = 0;
    int n_messages = DATA_SIZE_PER_THREAD/MESSAGE_SIZE;
    int first_burst = (n_messages < NUM_SLOTS) ? n_messages:NUM_SLOTS;
    for (int i=0; i<first_burst; i++){
        post_receive_client(res, i*MESSAGE_SIZE, i);    	    
        if (i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS>=DATA_SIZE)
	    next_offset[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
        else
	    next_offset[i] = i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS;
        ret = post_send_client(res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, i*MESSAGE_SIZE, next_offset[i], i);
        if (ret)
        {
	    fprintf(stderr, "failed to post SR\n");
	    exit(1);
        }
    }
    handle_recv(res, first_burst);
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
		static struct option long_options[] = {
			{.name = "port", .has_arg = 1, .val = 'p'},
			{.name = "ib-dev", .has_arg = 1, .val = 'd'},
			{.name = "ib-port", .has_arg = 1, .val = 'i'},
			{.name = "gid-idx", .has_arg = 1, .val = 'g'},
			{.name = "service-level", .has_arg = 1, .val = 's'},
			{.name = "desired-rate", .has_arg = 1, .val = 'r'},
			{.name = "help", .has_arg = 0, .val = '\0'},
			{.name = NULL, .has_arg = 0, .val = '\0'}
        };
		c = getopt_long(argc, argv, "p:d:i:g:s:r:h:", long_options, NULL);
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
		goto main_exit;
	}
	/* connect the QPs */
	if (connect_qp(&res, client_config))
	{
		fprintf(stderr, "failed to connect QPs\n");
		goto main_exit;
	}
	printf("Connected.\n");
	/* begin to send data */
	int num_rounds = 10;
        int round = 0;
	struct timeval cur_time;
        unsigned long start_time_msec;
        unsigned long diff_time_msec;
	uint32_t * next_offset = (uint32_t *)malloc(sizeof(uint32_t)*NUM_SLOTS);
	while(round<num_rounds){
	    for (int i = 0; i< DATA_SIZE; i++)
	        res.buf[i] = 'a'+i%10;
	    gettimeofday(&cur_time, NULL);
	    /*
	    int n_messages = DATA_SIZE/MESSAGE_SIZE;
            int first_burst = (n_messages < NUM_SLOTS) ? n_messages:NUM_SLOTS;
	    for (int i=0; i<first_burst; i++){
	        post_receive_client(&res, i*MESSAGE_SIZE, i);    	    
	        if (i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS>=DATA_SIZE)
	            next_offset[i] = (UINT32_MAX/MESSAGE_SIZE/NUM_SLOTS-1)*NUM_SLOTS*MESSAGE_SIZE+i*MESSAGE_SIZE;
	        else
	            next_offset[i] = i*MESSAGE_SIZE+MESSAGE_SIZE*NUM_SLOTS;
	        ret = post_send_client(&res, IBV_WR_RDMA_WRITE_WITH_IMM, MESSAGE_SIZE, i*MESSAGE_SIZE, next_offset[i], i);
	        if (ret)
	        {
		    fprintf(stderr, "failed to post SR\n");
		    rc = 1;
		    goto main_exit;
	        }
	    }
	    handle_recv(&res, first_burst);
	    */
	    process_per_thread(&res, next_offset);
	    start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	    gettimeofday(&cur_time, NULL);
	    diff_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000) - start_time_msec;
	    fprintf(stdout, "data size: %d Bytes; time: %ld ms; thoughput: %f Gbps\n", DATA_SIZE ,diff_time_msec,(DATA_SIZE)*8.0/1000000/diff_time_msec);
	    round++;
	}
	rc = 0;
main_exit:
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
