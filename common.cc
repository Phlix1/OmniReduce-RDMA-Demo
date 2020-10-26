#include "common.h"
/******************************************************************************
* Function: sock_connect
*
* Input
* servername URL of server to connect to (NULL for server mode)
* port port of service
*
* Output
* none
*
* Returns
* socket (fd) on success, negative error code on failure
*
* Description
* Connect a socket. If servername is specified a client connection will be
* initiated to the indicated server and port. Otherwise listen on the
* indicated port for an incoming connection.
*
******************************************************************************/
std::unordered_map<uint32_t, uint32_t> qp_num_revert {};
std::unordered_map<uint32_t, uint32_t> qp_num_to_peerid {};
int get_workerid_by_qp_num(uint32_t qp_num)
{
    return qp_num_to_peerid[qp_num];
}
int sock_connect(struct resources *res, struct config_t config)
{
	struct addrinfo *resolved_addr = NULL;
	struct addrinfo *iterator;
	char service[6];
	int sockfd = -1;
	int listenfd = 0;
	int c;
	struct sockaddr_in client;
	char ipAddr[INET_ADDRSTRLEN];
	int tmp;
	int serverid = 0;
	struct addrinfo hints =
		{
			.ai_flags = AI_PASSIVE,
			.ai_family = AF_INET,
			.ai_socktype = SOCK_STREAM};
	if (sprintf(service, "%d", config.tcp_port) < 0)
		goto sock_connect_exit;
	if (!config.isServer)
	{
	    for (int i=0; i<config.num_peers; i++)
	    {
		serverid = i;
	        /* Resolve DNS address, use sockfd as temp storage */
	        sockfd = getaddrinfo(config.peer_names[i], service, &hints, &resolved_addr);
	        if (sockfd < 0)
	        {
		    fprintf(stderr, "%s for %s:%d\n", gai_strerror(sockfd), config.peer_names[i], config.tcp_port);
		    goto sock_connect_exit;
	        }
	        /* Search through results and find the one we want */
	        for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
	        {
		    sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
		    if (sockfd >= 0)
		    {
			/* Client mode. Initiate connection to remote */
			if ((tmp = connect(sockfd, iterator->ai_addr, iterator->ai_addrlen)))
			{
				fprintf(stdout, "failed connect \n");
				close(sockfd);
				sockfd = -1;
			}
		    }
	        }
	        res->socks[i] = sockfd;
	    }
	}
	else
	{
	    /* Resolve DNS address, use sockfd as temp storage */
	    sockfd = getaddrinfo(NULL, service, &hints, &resolved_addr);
	    if (sockfd < 0)
	    {
		fprintf(stderr, "%s for NULL:%d\n", gai_strerror(sockfd), config.tcp_port);
		goto sock_connect_exit;
	    }
	    /* Search through results and find the one we want */
	    for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
	    {
		sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
		if (sockfd >= 0)
		{
			/* Server mode. Set up listening socket an accept a connection */
			listenfd = sockfd;
			sockfd = -1;
			if (bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
				goto sock_connect_exit;
			listen(listenfd, 1);
			//sockfd = accept(listenfd, NULL, 0);
			c = sizeof(struct sockaddr_in);
			int connect_num = 0;
			while (connect_num<config.num_peers) {
				sockfd = accept(listenfd, (struct sockaddr *)&client, (socklen_t*)&c);
				for(int i=0; i<config.num_peers; i++){
					const char* temp = inet_ntop(AF_INET, &client.sin_addr, ipAddr, sizeof(ipAddr));
					if (strcmp(config.peer_names[i], temp)==0){
					    printf("connected peer address = %s; index = %d\n", temp, i);
					    res->socks[i] = sockfd;
					}
				}
				connect_num++;
			}
		}
	    }
	}
sock_connect_exit:
	if (listenfd)
		close(listenfd);
	if (resolved_addr)
		freeaddrinfo(resolved_addr);
	if (sockfd < 0)
	{
		if (!config.isServer)
			fprintf(stderr, "Couldn't connect to %s:%d\n", config.peer_names[serverid], config.tcp_port);
		else
		{
			perror("server accept");
			fprintf(stderr, "accept() failed\n");
		}
	}
	return sockfd;
}
/******************************************************************************
* Function: sock_sync_data
*
* Input
* sock socket to transfer data on
* xfer_size size of data to transfer
* local_data pointer to data to be sent to remote
*
* Output
* remote_data pointer to buffer to receive remote data
*
* Returns
* 0 on success, negative error code on failure
*
* Description
* Sync data across a socket. The indicated local data will be sent to the
* remote. It will then wait for the remote to send its data back. It is
* assumed that the two sides are in sync and call this function in the proper
* order. Chaos will ensue if they are not. :)
*
* Also note this is a blocking function and will wait for the full data to be
* received from the remote.
*
******************************************************************************/
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
{
	int rc;
	int read_bytes = 0;
	int total_read_bytes = 0;
	rc = write(sock, local_data, xfer_size);
	if (rc < xfer_size)
		fprintf(stderr, "Failed writing data during sock_sync_data\n");
	else
		rc = 0;
	while (!rc && total_read_bytes < xfer_size)
	{
		read_bytes = read(sock, remote_data, xfer_size);
		if (read_bytes > 0)
			total_read_bytes += read_bytes;
		else
			rc = read_bytes;
	}
	return rc;
}
/******************************************************************************
End of socket operations
******************************************************************************/
/* poll_cq */
/******************************************************************************
* Function: poll_cq
*
* Input
* res pointer to resources structure
*
* Output
* none
*
* Returns
* none
*
* Description
* Poll the completion queue.
*
******************************************************************************/
void *poll_cq(struct resources *res)
{
	struct timeval cur_time;
	unsigned long start_time_msec;
	unsigned long diff_time_msec;
	unsigned long recv_cnt = 0, last_recv_cnt = 0;

	int finished = 0;
	struct ibv_cq *cq;
	void *ctx = NULL;
	struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
#ifdef DEBUG
	fprintf(stdout, "thread poll_cq has been created successfully.\n");
#endif

	gettimeofday(&cur_time, NULL);
	start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
  	while (1) {
    	ibv_get_cq_event(res->event_channel, &cq, &ctx);
		// assert(cq == res->cq);
    	ibv_ack_cq_events(cq, 1);
    	ibv_req_notify_cq(cq, 0);

    	int ne = ibv_poll_cq(cq, MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
		for (int i = 0; i < ne; ++i)
		{
      		if (wc[i].status == IBV_WC_SUCCESS)
	  		{
				if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) { 
					uint32_t imm_data = wc[i].imm_data;
					// struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc[i]->wr_id);
					res->buf[imm_data] = '\0';
					recv_cnt += imm_data;
#ifdef DEBUG
					fprintf(stdout, "IBV_WC_RECV_RDMA_WITH_IMM : %d, %u\n", ne, imm_data);
#endif
					if (imm_data == 0)
					{
						finished = 1;
						int ret = post_send(res, IBV_WR_RDMA_WRITE_WITH_IMM, 0, 0); // signal for sender
						if (ret)
						{
							fprintf(stderr, "failed to post SR to signal sender\n");
						}
						break;
					}

					gettimeofday(&cur_time, NULL);
					diff_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000) - start_time_msec;
					if (diff_time_msec >= 1000)
					{
						fprintf(stdout, "ingress rate: %5.1f Gbps\n", (recv_cnt-last_recv_cnt)*8.0/1000000/diff_time_msec);
						last_recv_cnt = recv_cnt;
						gettimeofday(&cur_time, NULL);
						start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
					}
					post_receive(res); // put back a recv wr
				}
				else if (wc[i].opcode == IBV_WC_RECV) {
#ifdef DEBUG
					fprintf(stdout, "IBV_WC_RECV\n");
#endif
					post_receive(res); // put back a recv wr
				}
				/* else if (wc[i].opcode == IBV_WC_RDMA_WRITE) // RDMA Write operation for a WR that was posted to the Send Queue
					fprintf(stdout, "%u\n", wc[i].imm_data); */
			}
			else
			{
				fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n", wc[i].status,
							wc[i].vendor_err);
			}
    	}
		if (finished)
			break;
  	}
  	return NULL;
}
/******************************************************************************
* Function: post_send
*
* Input
* res pointer to resources structure
* opcode IBV_WR_SEND, IBV_WR_RDMA_READ or IBV_WR_RDMA_WRITE
*
* Output
* none
*
* Returns
* 0 on success, error code on failure
*
* Description
* This function will create and post a send work request
******************************************************************************/
int post_send(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset)
{
	struct ibv_send_wr sr;
	struct ibv_sge sge;
	struct ibv_send_wr *bad_wr = NULL;
	int rc;
	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	DATA_TYPE *tmp = res->buf+offset;
	//sge.addr = (uintptr_t)res->buf;
	sge.addr = (uintptr_t)tmp;
	sge.length = len*sizeof(DATA_TYPE);
	sge.lkey = res->mr->lkey;
	/* prepare the send work request */
	memset(&sr, 0, sizeof(sr));
	//sr.next = NULL;
	sr.wr_id = 0;
	sr.sg_list = &sge;
	sr.num_sge = 1;
	sr.opcode = opcode;
	sr.send_flags = IBV_SEND_SIGNALED;
	if (opcode != IBV_WR_SEND)
	{
		sr.wr.rdma.remote_addr = res->remote_props.addr;
		sr.wr.rdma.rkey = res->remote_props.rkey;
	}
	if (opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
	{
		sr.imm_data = len;
#ifdef DEBUG
		fprintf(stdout, "IBV_WR_RDMA_WRITE_WITH_IMM : %u\n", len);
#endif
	}
	/* there is a Receive Request in the responder side, so we won't get any into RNR flow */
	rc = ibv_post_send(res->qp[0], &sr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post SR %d\n", rc);
#ifdef DEBUG
	else
	{
		switch (opcode)
		{
		case IBV_WR_SEND:
			fprintf(stdout, "Send Request was posted\n");
			break;
		case IBV_WR_RDMA_READ:
			fprintf(stdout, "RDMA Read Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE:
			fprintf(stdout, "RDMA Write Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE_WITH_IMM:
			fprintf(stdout, "RDMA Write with IMM Request was posted\n");
			break;
		default:
			fprintf(stdout, "Unknown Request was posted\n");
			break;
		}
	}
#endif
	return rc;
}
int post_send_client(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset, uint32_t imm, int slot, uint32_t qp_num)
{
	struct ibv_send_wr sr;
	struct ibv_sge sge;
	struct ibv_send_wr *bad_wr = NULL;
	int qid;
	int mid;
	if(qp_num==0){
	    qid = (slot/NUM_SLOTS)*NUM_QPS*res->num_socks+slot%(NUM_QPS*res->num_socks);
	    mid = qp_num_to_peerid[res->qp[qid]->qp_num];
	}
	else {
	    qid = qp_num_revert[qp_num];
	    mid = qp_num_to_peerid[qp_num];
	}
#ifdef DEBUG
	//std::cout<<"qp num dict: "<<std::endl;
	//for ( auto it = qp_num_revert.begin(); it != qp_num_revert.end(); ++it )
        //    std::cout << " " << it->first << ":" << it->second;
	//std::cout<<std::endl;
	std::cout<<"send data with QP "<<qid<<"; QP num: ";
	printf("0x%x\n", res->qp[qid]->qp_num);
#endif
	int rc;
	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	DATA_TYPE *tmp = res->buf+offset;
	//sge.addr = (uintptr_t)res->buf;
	sge.addr = (uintptr_t)tmp;
	sge.length = len*sizeof(DATA_TYPE);
	sge.lkey = res->mr->lkey;
	/* prepare the send work request */
	memset(&sr, 0, sizeof(sr));
	//sr.next = NULL;
	sr.wr_id = 0;
	sr.sg_list = &sge;
	sr.num_sge = 1;
	sr.opcode = opcode;
	sr.send_flags = IBV_SEND_SIGNALED;
	if (opcode != IBV_WR_SEND)
	{
		//sr.wr.rdma.remote_addr = res->remote_props.addr+MESSAGE_SIZE*slot*sizeof(DATA_TYPE);
		//sr.wr.rdma.rkey = res->remote_props.rkey;
		sr.wr.rdma.remote_addr = res->remote_props_array[mid].addr+MESSAGE_SIZE*slot*sizeof(DATA_TYPE)+MESSAGE_SIZE*NUM_SLOTS*NUM_THREADS*res->myId*sizeof(DATA_TYPE);
		sr.wr.rdma.rkey = res->remote_props_array[mid].rkey;
	}
	if (opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
	{
		sr.imm_data = imm;
#ifdef DEBUG
		//fprintf(stdout, "IBV_WR_RDMA_WRITE_WITH_IMM : %u, %s\n", imm, res->buf);
#endif
	}
	/* there is a Receive Request in the responder side, so we won't get any into RNR flow */
	rc = ibv_post_send(res->qp[qid], &sr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post SR %d\n", rc);
#ifdef DEBUG
	else
	{
		switch (opcode)
		{
		case IBV_WR_SEND:
			fprintf(stdout, "Send Request was posted\n");
			break;
		case IBV_WR_RDMA_READ:
			fprintf(stdout, "RDMA Read Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE:
			fprintf(stdout, "RDMA Write Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE_WITH_IMM:
			fprintf(stdout, "RDMA Write with IMM Request was posted\n");
			break;
		default:
			fprintf(stdout, "Unknown Request was posted\n");
			break;
		}
	}
#endif
	return rc;
}
int post_send_server(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset, uint32_t imm, int slot, uint32_t qp_num, int set)
{
#ifdef DEBUG
	//std::cout<<"qp num dict: "<<std::endl;
	//for ( auto it = qp_num_revert.begin(); it != qp_num_revert.end(); ++it )
        //    std::cout << " " << it->first << ":" << it->second;
	//std::cout<<std::endl;
	std::cout<<"send data with QP "<<qp_num_revert[qp_num]<<"; QP num: ";
	printf("0x%x\n", qp_num);
#endif
	struct ibv_send_wr sr;
	struct ibv_sge sge;
	struct ibv_send_wr *bad_wr = NULL;
	int qid;
	int mid;
	if(qp_num==0){
	    qid = (slot/NUM_SLOTS)*NUM_QPS*res->num_socks+slot%(NUM_QPS*res->num_socks);
	    mid = qp_num_to_peerid[res->qp[qid]->qp_num];
	}
	else {
	    qid = qp_num_revert[qp_num];
	    mid = qp_num_to_peerid[qp_num];
	}
	int rc;
	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	DATA_TYPE *tmp = res->buf+NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS*(res->num_socks+set)+slot*MESSAGE_SIZE;
	//sge.addr = (uintptr_t)res->buf;
	sge.addr = (uintptr_t)tmp;
	sge.length = len*sizeof(DATA_TYPE);
	sge.lkey = res->mr->lkey;
	/* prepare the send work request */
	memset(&sr, 0, sizeof(sr));
	//sr.next = NULL;
	sr.wr_id = 0;
	sr.sg_list = &sge;
	sr.num_sge = 1;
	sr.opcode = opcode;
	sr.send_flags = IBV_SEND_SIGNALED;
	if (opcode != IBV_WR_SEND)
	{
		//sr.wr.rdma.remote_addr = res->remote_props.addr+offset*sizeof(DATA_TYPE);
		//sr.wr.rdma.rkey = res->remote_props.rkey;
                sr.wr.rdma.remote_addr = res->remote_props_array[mid].addr+offset*sizeof(DATA_TYPE);
                sr.wr.rdma.rkey = res->remote_props_array[mid].rkey;
	}
	if (opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
	{
		sr.imm_data = imm;
#ifdef DEBUG
		fprintf(stdout, "IBV_WR_RDMA_WRITE_WITH_IMM : %u\n", imm);
#endif
	}
	/* there is a Receive Request in the responder side, so we won't get any into RNR flow */
	rc = ibv_post_send(res->qp[qid], &sr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post SR %d\n", rc);
#ifdef DEBUG
	else
	{
		switch (opcode)
		{
		case IBV_WR_SEND:
			fprintf(stdout, "Send Request was posted\n");
			break;
		case IBV_WR_RDMA_READ:
			fprintf(stdout, "RDMA Read Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE:
			fprintf(stdout, "RDMA Write Request was posted\n");
			break;
		case IBV_WR_RDMA_WRITE_WITH_IMM:
			fprintf(stdout, "RDMA Write with IMM Request was posted\n");
			break;
		default:
			fprintf(stdout, "Unknown Request was posted\n");
			break;
		}
	}
#endif
	return rc;
}
/******************************************************************************
* Function: post_receive
*
* Input
* res pointer to resources structure
*
* Output
* none
*
* Returns
* 0 on success, error code on failure
*
* Description
*
******************************************************************************/
int post_receive(struct resources *res)
{
	struct ibv_recv_wr rr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	int rc;

	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)res->buf;
	sge.length = MESSAGE_SIZE;
	sge.lkey = res->mr->lkey;
	/* prepare the receive work request */
	memset(&rr, 0, sizeof(rr));
	// rr.next = NULL;
	rr.wr_id = 0;
	rr.sg_list = &sge;
	rr.num_sge = 1;
	/* post the Receive Request to the RQ */
	rc = ibv_post_recv(res->qp[0], &rr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post RR\n");
#ifdef DEBUG
	else
		fprintf(stdout, "Receive Request was posted\n");
#endif
	return rc;
}
int post_receive_server(struct resources *res, int qp_id, uint32_t qp_num)
{
	struct ibv_recv_wr rr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	int rc;

	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	//sge.addr = (uintptr_t)(res->buf+slot*MESSAGE_SIZE);
	sge.addr = (uintptr_t)(res->buf);
	sge.length = MESSAGE_SIZE*sizeof(DATA_TYPE);
	sge.lkey = res->mr->lkey;
	/* prepare the receive work request */
	memset(&rr, 0, sizeof(rr));
	// rr.next = NULL;
	rr.wr_id = 0;
	rr.sg_list = &sge;
	rr.num_sge = 1;
	/* post the Receive Request to the RQ */
	//rc = ibv_post_recv(res->qp[(slot/NUM_SLOTS)*NUM_QPS+slot%NUM_QPS], &rr, &bad_wr);
	if (qp_num==0){
	    rc = ibv_post_recv(res->qp[qp_id], &rr, &bad_wr);
#ifdef DEBUG
	    std::cout<<"receive was post in QP "<<qp_id<<std::endl;
#endif
	}
	else
	{
	    rc = ibv_post_recv(res->qp[qp_num_revert[qp_num]], &rr, &bad_wr);
#ifdef DEBUG
	    std::cout<<"receive was post in QP "<<qp_num_revert[qp_num]<<std::endl;
#endif
	}
	if (rc)
		fprintf(stderr, "failed to post RR\n");
#ifdef DEBUG
	else
		fprintf(stdout, "Receive Request was posted\n");
#endif
	return rc;
}
int post_receive_client(struct resources *res, uint32_t offset, int slot, uint32_t qp_num)
{
	struct ibv_recv_wr rr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	int rc;
	int qid;
	if(qp_num==0){
	    qid = (slot/NUM_SLOTS)*NUM_QPS*res->num_socks+slot%(NUM_QPS*res->num_socks);
	}
	else {
	    qid = qp_num_revert[qp_num];
	}
	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	//DATA_TYPE*tmp = res->buf + offset*sizeof(DATA_TYPE);
	sge.addr = (uintptr_t)res->buf;
	//sge.addr = (uintptr_t)tmp;
	sge.length = MESSAGE_SIZE*sizeof(DATA_TYPE);
	sge.lkey = res->mr->lkey;
	/* prepare the receive work request */
	memset(&rr, 0, sizeof(rr));
	// rr.next = NULL;
	rr.wr_id = 0;
	rr.sg_list = &sge;
	rr.num_sge = 1;
	/* post the Receive Request to the RQ */
	rc = ibv_post_recv(res->qp[qid], &rr, &bad_wr);
	if (rc)
		fprintf(stderr, "failed to post RR\n");
#ifdef DEBUG
	else
		fprintf(stdout, "Receive Request was posted\n");
#endif
	return rc;
}
/******************************************************************************
* Function: resources_init
*
* Input
* res pointer to resources structure
*
* Output
* res is initialized
*
* Returns
* none
*
* Description
* res is initialized to default values
******************************************************************************/
void resources_init(struct resources *res)
{
	memset(res, 0, sizeof *res);
	res->sock_status = -1;
}
/******************************************************************************
* Function: resources_create
*
* Input
* res pointer to resources structure to be filled in
*
* Output
* res filled in with resources
*
* Returns
* 0 on success, 1 on failure
*
* Description
*
* This function creates and allocates all necessary system resources. These
* are stored in res.
*****************************************************************************/
int resources_create(struct resources *res, struct config_t config)
{
	struct ibv_device **dev_list = NULL;
	struct ibv_qp_init_attr qp_init_attr[NUM_THREADS];
	//struct ibv_qp_init_attr qp_init_attr;
	//struct ibv_qp_init_attr qp_init_attr1;
	struct ibv_device *ib_dev = NULL;
	size_t size;
	int i;
	int mr_flags = 0;
	int cq_size = 0;
	int num_devices;
	int rc = 0;
	int cycle_buffer = sysconf(_SC_PAGESIZE);
	res->num_socks = config.num_peers;
	/* if client side */
	if (!config.isServer)
	{
		res->sock_status = sock_connect(res, config);
		std::cout<<"###socket number: "<<res->num_socks<<std::endl;
		for (int t=0; t<res->num_socks; t++){
			std::cout<<"socket "<<t<<" -> "<<res->socks[t]<<std::endl; 
		}
		if (res->sock_status < 0)
		{
			fprintf(stderr, "failed to establish TCP connection to server, port %d\n",
					config.tcp_port);
			rc = -1;
			goto resources_create_exit;
		}
	}
	else
	{
#ifdef DEBUG
		fprintf(stdout, "waiting on port %d for TCP connection\n", config.tcp_port);
#endif
		res->sock_status = sock_connect(res, config);
		std::cout<<"###socket number: "<<res->num_socks<<std::endl;
		for (int t=0; t<res->num_socks; t++){
			std::cout<<"socket "<<t<<" -> "<<res->socks[t]<<std::endl; 
		}
		if (res->sock_status < 0)
		{
			fprintf(stderr, "failed to establish TCP connection with client on port %d\n",
					config.tcp_port);
			rc = -1;
			goto resources_create_exit;
		}
	}
#ifdef DEBUG
	fprintf(stdout, "TCP connection was established\n");
	fprintf(stdout, "searching for IB devices in host\n");
#endif
	/* get device names in the system */
	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list)
	{
		fprintf(stderr, "failed to get IB devices list\n");
		rc = 1;
		goto resources_create_exit;
	}
	/* if there isn't any IB device in host */
	if (!num_devices)
	{
		fprintf(stderr, "found %d device(s)\n", num_devices);
		rc = 1;
		goto resources_create_exit;
	}
#ifdef DEBUG
	fprintf(stdout, "found %d device(s)\n", num_devices);
#endif
	/* search for the specific device we want to work with */
	for (i = 0; i < num_devices; i++)
	{
		if (!config.dev_name)
		{
			config.dev_name = strdup(ibv_get_device_name(dev_list[i]));
			fprintf(stdout, "device not specified, using first one found: %s\n", config.dev_name);
		}
		if (!strcmp(ibv_get_device_name(dev_list[i]), config.dev_name))
		{
			ib_dev = dev_list[i];
			break;
		}
	}
	/* if the device wasn't found in host */
	if (!ib_dev)
	{
		fprintf(stderr, "IB device %s wasn't found\n", config.dev_name);
		rc = 1;
		goto resources_create_exit;
	}
	/* get device handle */
	res->ib_ctx = ibv_open_device(ib_dev);
	if (!res->ib_ctx)
	{
		fprintf(stderr, "failed to open device %s\n", config.dev_name);
		rc = 1;
		goto resources_create_exit;
	}
	/* We are now done with device list, free it */
	ibv_free_device_list(dev_list);
	dev_list = NULL;
	ib_dev = NULL;
	/* query port properties */
	if (ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr))
	{
		fprintf(stderr, "ibv_query_port on port %u failed\n", config.ib_port);
		rc = 1;
		goto resources_create_exit;
	}
	/* allocate Protection Domain */
	res->pd = ibv_alloc_pd(res->ib_ctx);
	if (!res->pd)
	{
		fprintf(stderr, "ibv_alloc_pd failed\n");
		rc = 1;
		goto resources_create_exit;
	}
	
	cq_size = MAX_CONCURRENT_WRITES * 2;
	for(int i=0; i<NUM_THREADS; i++)
	{
	    res->cq[i] = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
	    if (!res->cq[i])
	    {
		fprintf(stderr, "failed to create CQ with %u entries\n", cq_size);
		rc = 1;
		goto resources_create_exit;
	    }
	}
	/* allocate the memory buffer that will hold the data */
	if (!config.isServer)
	    size = DATA_SIZE;
	else
	    size = res->num_socks*NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS+NUM_SLOTS*MESSAGE_SIZE*NUM_THREADS*2;
	
	rc = posix_memalign(reinterpret_cast<void**>(&res->buf), cycle_buffer, size*sizeof(DATA_TYPE));
	//res->buf = (DATA_TYPE *)malloc(size*sizeof(DATA_TYPE));
	if (rc!=0)
	{
		fprintf(stderr, "failed to malloc %Zu bytes to memory buffer\n", size);
		rc = 1;
		goto resources_create_exit;
	}
	memset(res->buf, 0, size*sizeof(DATA_TYPE));
	/* register the memory buffer */
	mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	res->mr = ibv_reg_mr(res->pd, res->buf, size*sizeof(DATA_TYPE), mr_flags);
	if (!res->mr)
	{
		fprintf(stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
		rc = 1;
		goto resources_create_exit;
	}
#ifdef DEBUG
	fprintf(stdout, "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
			res->buf, res->mr->lkey, res->mr->rkey, mr_flags);
#endif
        for(int i=0; i<NUM_THREADS; i++)
	{
	    memset(&qp_init_attr[i], 0, sizeof(ibv_qp_init_attr));
	    qp_init_attr[i].qp_type = IBV_QPT_RC;
	    //qp_init_attr[i].qp_type = IBV_QPT_UC;
	    qp_init_attr[i].sq_sig_all = 1;
	    qp_init_attr[i].send_cq = res->cq[i];
	    qp_init_attr[i].recv_cq = res->cq[i];
	    qp_init_attr[i].cap.max_send_wr = QUEUE_DEPTH_DEFAULT;
	    qp_init_attr[i].cap.max_recv_wr = QUEUE_DEPTH_DEFAULT;
	    qp_init_attr[i].cap.max_send_sge = 1;
	    qp_init_attr[i].cap.max_recv_sge = 1;
	}
        res->qp = (struct ibv_qp **)malloc(res->num_socks*NUM_QPS*NUM_THREADS*sizeof(struct ibv_qp *));	
	/* create the Queue Pair */
	for(int i=0; i<res->num_socks*NUM_QPS*NUM_THREADS; i++)
	{
	    res->qp[i] = ibv_create_qp(res->pd, &qp_init_attr[i/(NUM_QPS*res->num_socks)]);
	    if (!res->qp[i])
	    {
		fprintf(stderr, "failed to create QP\n");
		rc = 1;
		goto resources_create_exit;
	    }
	    qp_num_revert.insert(std::make_pair(res->qp[i]->qp_num, i));
	}
#ifdef DEBUG
	for(int i=0; i<res->num_socks*NUM_QPS*NUM_THREADS; i++)
	    fprintf(stdout, "QP was created, QP number=0x%x\n", res->qp[i]->qp_num);
#endif
resources_create_exit:
	if (rc)
	{
		/* Error encountered, cleanup */
	        for(int i=0; i<res->num_socks*NUM_QPS*NUM_THREADS; i++){
		    if (res->qp[i])
		    {
			ibv_destroy_qp(res->qp[i]);
			res->qp[i] = NULL;
		    }
		}
		if (res->mr)
		{
			ibv_dereg_mr(res->mr);
			res->mr = NULL;
		}
		if (res->buf)
		{
			free(res->buf);
			res->buf = NULL;
		}
                for(int i=0; i<NUM_THREADS; i++) {
		    if (res->cq[i])
		    {
			ibv_destroy_cq(res->cq[i]);
			res->cq[i] = NULL;
		    }
		}
		if (res->pd)
		{
			ibv_dealloc_pd(res->pd);
			res->pd = NULL;
		}
		if (res->ib_ctx)
		{
			ibv_close_device(res->ib_ctx);
			res->ib_ctx = NULL;
		}
		if (dev_list)
		{
			ibv_free_device_list(dev_list);
			dev_list = NULL;
		}
		if (res->sock_status >= 0)
		{
			for (int i=0; i<res->num_socks; i++)
			    if (close(res->socks[i]))
				fprintf(stderr, "failed to close socket\n");
			res->sock_status = -1;
		}
	}

#ifdef DEBUG
        fprintf(stdout, "Resouces were created, status=%d\n", rc);
#endif
	return rc;
}
/******************************************************************************
* Function: modify_qp_to_init
*
* Input
* qp QP to transition
*
* Output
* none
*
* Returns
* 0 on success, ibv_modify_qp failure code on failure
*
* Description
* Transition a QP from the RESET to INIT state
******************************************************************************/
int modify_qp_to_init(struct ibv_qp *qp, struct config_t config)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.port_num = config.ib_port;
	attr.pkey_index = 0;
	attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
		fprintf(stderr, "failed to modify QP state to INIT\n");
	return rc;
}
/******************************************************************************
* Function: modify_qp_to_rtr
*
* Input
* qp QP to transition
* remote_qpn remote QP number
* dlid destination LID
* dgid destination GID (mandatory for RoCEE)
*
* Output
* none
*
* Returns
* 0 on success, ibv_modify_qp failure code on failure
*
* Description
* Transition a QP from the INIT to RTR state, using the specified QP number
******************************************************************************/
int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid, struct config_t config)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = IBV_MTU_1024; // default MTU is 1024
	attr.dest_qp_num = remote_qpn;
	attr.rq_psn = 0;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer = 0x12;
	attr.ah_attr.is_global = 0;
	attr.ah_attr.dlid = dlid;
	attr.ah_attr.sl = config.sl;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num = config.ib_port;
	if (config.gid_idx >= 0)
	{
		attr.ah_attr.is_global = 1;
		attr.ah_attr.port_num = config.ib_port;
		memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
		attr.ah_attr.grh.flow_label = 0;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.sgid_index = config.gid_idx;
		attr.ah_attr.grh.traffic_class = 0;
	}
	flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
		fprintf(stderr, "failed to modify QP state to RTR\n");
	return rc;
}
/******************************************************************************
* Function: modify_qp_to_rts
*
* Input
* qp QP to transition
*
* Output
* none
*
* Returns
* 0 on success, ibv_modify_qp failure code on failure
*
* Description
* Transition a QP from the RTR to RTS state
******************************************************************************/
int modify_qp_to_rts(struct ibv_qp *qp, struct config_t config)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = 14;
	attr.retry_cnt = 7;
	attr.rnr_retry = 7;
	attr.sq_psn = 0;
	attr.max_rd_atomic = 1;
	flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
		fprintf(stderr, "failed to modify QP state to RTS\n");
	return rc;
}
/******************************************************************************
* Function: connect_qp
*
* Input
* res pointer to resources structure
*
* Output
* none
*
* Returns
* 0 on success, error code on failure
*
* Description
* Connect the QP. Transition the server side to RTR, sender side to RTS
******************************************************************************/
int connect_qp(struct resources *res, struct config_t config)
{
	//struct cm_con_data_t local_con_data;
	struct cm_con_data_t local_con_datas[res->num_socks];
	//struct cm_con_data_t remote_con_data;
	struct cm_con_data_t remote_con_datas[res->num_socks];
	struct cm_con_data_t tmp_con_data;
	int rc = 0;
	char temp_char;
	union ibv_gid my_gid;
	if (config.gid_idx >= 0)
	{
		rc = ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
		if (rc)
		{
			fprintf(stderr, "could not get gid for port %d, index %d\n", config.ib_port, config.gid_idx);
			return rc;
		}
	}
	else
	memset(&my_gid, 0, sizeof my_gid);
	/* exchange using TCP sockets info required to connect QPs */
	
	//local_con_data.addr = htonll((uintptr_t)res->buf);
	//local_con_data.rkey = htonl(res->mr->rkey);
	//for(int i=0; i<NUM_QPS*NUM_THREADS; i++)
	//    local_con_data.qp_num[i] = htonl(res->qp[i]->qp_num);
	//local_con_data.lid = htons(res->port_attr.lid);
	//memcpy(local_con_data.gid, &my_gid, 16);
	for (int i=0; i<res->num_socks; i++)
	{
	    local_con_datas[i].remoteId = i;
	    local_con_datas[i].num_machines = res->num_socks;
	    local_con_datas[i].addr = htonll((uintptr_t)res->buf);
	    local_con_datas[i].rkey = htonl(res->mr->rkey);
	    for(int j=0; j<res->num_socks*NUM_QPS*NUM_THREADS; j++)
	        local_con_datas[i].qp_num[j] = htonl(res->qp[j]->qp_num);
	    memcpy(local_con_datas[i].gid, &my_gid, 16);
#ifdef DEBUG
	    fprintf(stdout, "Local address = 0x%" PRIx64 "\n", local_con_datas[i].addr);
	    fprintf(stdout, "Local rkey = 0x%x\n", local_con_datas[i].rkey);
	    for(int j=0; j<res->num_socks*NUM_QPS*NUM_THREADS; j++){
	        fprintf(stdout, "Local QP number = 0x%x\n", ntohl(local_con_datas[i].qp_num[j]));
	    }
	    fprintf(stdout, "Local LID = 0x%x\n", local_con_datas[i].lid);
#endif
	}
	for (int i=0; i<res->num_socks; i++)
	{
	    if (sock_sync_data(res->socks[i], sizeof(struct cm_con_data_t), (char *)&local_con_datas[i], (char *)&tmp_con_data) < 0)
	     {
		fprintf(stderr, "failed to exchange connection data between sides\n");
		rc = 1;
		goto connect_qp_exit;
	     }
	    if (i==0)
	    {
	        res->myId = tmp_con_data.remoteId;
		res->num_machines = tmp_con_data.num_machines;
	    }
	    else if(res->myId != tmp_con_data.remoteId || res->num_machines != tmp_con_data.num_machines)
	    {
	        fprintf(stderr, "machine ID or number error\n");
		rc = 1;
		goto connect_qp_exit;
	    }
	    remote_con_datas[i].remoteId = tmp_con_data.remoteId;
	    remote_con_datas[i].num_machines = tmp_con_data.num_machines;
	    remote_con_datas[i].addr = ntohll(tmp_con_data.addr);
	    remote_con_datas[i].rkey = ntohl(tmp_con_data.rkey);
	    for(int j=0; j<res->num_machines*NUM_QPS*NUM_THREADS; j++)
	        remote_con_datas[i].qp_num[j] = ntohl(tmp_con_data.qp_num[j]);
	    remote_con_datas[i].lid = ntohs(tmp_con_data.lid);
	    memcpy(remote_con_datas[i].gid, tmp_con_data.gid, 16);
	    res->remote_props_array[i] = remote_con_datas[i];
#ifdef DEBUG
	    fprintf(stdout, "Remote address = 0x%" PRIx64 "\n", res->remote_props_array[i].addr);
	    fprintf(stdout, "Remote rkey = 0x%x\n", res->remote_props_array[i].rkey);
	    for(int j=0; j<res->num_machines*NUM_QPS*NUM_THREADS; j++){
	        fprintf(stdout, "Remote QP number = 0x%x\n", res->remote_props_array[i].qp_num[j]);
	    }
	    fprintf(stdout, "Remote LID = 0x%x\n", res->remote_props_array[i].lid);
	    if (config.gid_idx >= 0)
	    {
		uint8_t *p = res->remote_props_array[i].gid;
		fprintf(stdout, "Remote GID =%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n ",p[0],
				  p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	    }
#endif
	}
	//remote_con_data.remoteId = tmp_con_data.remoteId;
	//remote_con_data.num_machines = tmp_con_data.num_machines;
	//remote_con_data.addr = ntohll(tmp_con_data.addr);
	//remote_con_data.rkey = ntohl(tmp_con_data.rkey);
	//for(int i=0; i<res->num_socks*NUM_QPS*NUM_THREADS; i++)
	//    remote_con_data.qp_num[i] = ntohl(tmp_con_data.qp_num[i]);
	//remote_con_data.lid = ntohs(tmp_con_data.lid);
	//memcpy(remote_con_data.gid, tmp_con_data.gid, 16);
	/* save the remote side attributes, we will need it for the post SR */
	//res->remote_props = remote_con_data;

	std::cout<<"##### connection info #####"<<std::endl;
	for(int i=0; i<res->num_socks*NUM_QPS*NUM_THREADS; i++)
	{
	    int tid = i/(res->num_socks*NUM_QPS);
	    int mid = (i%(res->num_socks*NUM_QPS))%res->num_socks;
	    int qid = ((i%(res->num_socks*NUM_QPS))/res->num_socks)*res->num_machines+res->myId+tid*res->num_machines*NUM_QPS;
            qp_num_to_peerid.insert(std::make_pair(res->qp[i]->qp_num, mid));
//#ifdef DEBUG
	    //std::cout<<"thread ID: "<<tid<<"; peer ID: "<<mid<<"; QP ID: "<<qid<<std::endl;
	    if (config.isServer)
	    {
	        std::cout<<"Aggregator "<<res->myId<<": QP "<<i<<" <-----> "<<"Worker "<<mid<<": QP "<<qid<<std::endl;
	    }
	    else
	    {
	        std::cout<<"Worker "<<res->myId<<": QP "<<i<<" <-----> "<<"Aggregator "<<mid<<": QP "<<qid<<std::endl;
	    }
//#endif

	    /* modify the QP to init */
            rc = modify_qp_to_init(res->qp[i], config);
	    if (rc)
	    {
		fprintf(stderr, "change QP state to INIT failed\n");
		goto connect_qp_exit;
	    }
	
	    /* modify the QP to RTR */
	    //rc = modify_qp_to_rtr(res->qp[i], remote_con_data.qp_num[i], remote_con_data.lid, remote_con_data.gid, config);
	    rc = modify_qp_to_rtr(res->qp[i], res->remote_props_array[mid].qp_num[qid], res->remote_props_array[mid].lid, res->remote_props_array[mid].gid, config);
	    if (rc)
	    {
		fprintf(stderr, "failed to modify QP state to RTR\n");
		goto connect_qp_exit;
	    }
	    rc = modify_qp_to_rts(res->qp[i], config);
	    if (rc)
	    {
		fprintf(stderr, "failed to modify QP state to RTR\n");
		goto connect_qp_exit;
	    }
	}
	std::cout<<"##########"<<std::endl;
#ifdef DEBUG
	fprintf(stdout, "QP state was change to RTS\n");
#endif

	for (int i=0; i<res->num_socks; i++)
	{
	    /* sync to make sure that both sides are in states that they can connect to prevent packet loose */
	    if (sock_sync_data(res->socks[i], 1, (char *)"Q", &temp_char)) /* just send a dummy char back and forth */
	    {
		fprintf(stderr, "sync error after QPs are were moved to RTS\n");
		rc = 1;
	    }
	}
connect_qp_exit:
	return rc;
}
/******************************************************************************
* Function: resources_destroy
*
* Input
* res pointer to resources structure
*
* Output
* none
*
* Returns
* 0 on success, 1 on failure
*
* Description
* Cleanup and deallocate all resources used
******************************************************************************/
int resources_destroy(struct resources *res)
{
	int rc = 0;

	for(int i=0; i<res->num_socks*NUM_QPS*NUM_THREADS; i++)
	{
	    if (res->qp[i])
		if (ibv_destroy_qp(res->qp[i]))
		{
			fprintf(stderr, "failed to destroy QP\n");
			rc = 1;
		}
	}
	if (res->mr)
		if (ibv_dereg_mr(res->mr))
		{
			fprintf(stderr, "failed to deregister MR\n");
			rc = 1;
		}
	if (res->buf)
		free(res->buf);
	for(int i=0; i<NUM_THREADS; i++){
	    if (res->cq[i])
		if (ibv_destroy_cq(res->cq[i]))
		{
			fprintf(stderr, "failed to destroy CQ\n");
			rc = 1;
		}
	}
	if (res->pd)
		if (ibv_dealloc_pd(res->pd))
		{
			fprintf(stderr, "failed to deallocate PD\n");
			rc = 1;
		}
	if (res->ib_ctx)
		if (ibv_close_device(res->ib_ctx))
		{
			fprintf(stderr, "failed to close device context\n");
			rc = 1;
		}
	if (res->sock_status >= 0)
	{
		for (int i=0; i<res->num_socks; i++)
		    if (close(res->socks[i]))
			fprintf(stderr, "failed to close socket\n");
		res->sock_status = -1;
	}
	return rc;
}
/******************************************************************************
* Function: print_config
*
* Input
* none
*
* Output
* none
*
* Returns
* none
*
* Description
* Print out config information
******************************************************************************/
void print_config(struct config_t config)
{
	fprintf(stdout, " ------------------------------------------------\n");
	if (!config.isServer){
	    for(int i=0; i<config.num_peers; i++)
		fprintf(stdout, " Server %d : %s\n", i, config.peer_names[i]);
	}
	else {
	    for(int i=0; i<config.num_peers; i++)
		fprintf(stdout, " Client %d : %s\n", i, config.peer_names[i]);
	}
	fprintf(stdout, " TCP port : %u\n", config.tcp_port);
	fprintf(stdout, " Device name : \"%s\"\n", config.dev_name);
	fprintf(stdout, " IB port : %u\n", config.ib_port);	
	if (config.gid_idx >= 0)
		fprintf(stdout, " GID index : %u\n", config.gid_idx);
	fprintf(stdout, " Service level : %u\n", config.sl);
	fprintf(stdout, " ------------------------------------------------\n\n");
}

/******************************************************************************
* Function: usage
*
* Input
* argv0 command line arguments
*
* Output
* none
*
* Returns
* none
*
* Description
* print a description of command line syntax
******************************************************************************/
void usage(const char *argv0)
{
	fprintf(stdout, "Usage:\n");
	if (!strcmp(argv0, "./server"))
		fprintf(stdout, " %s start a server and wait for connection\n", argv0);
	else
		fprintf(stdout, " %s <host> connect to server at <host>\n", argv0);
	fprintf(stdout, "\n");
	fprintf(stdout, "Options:\n");
	fprintf(stdout, " -p, --port <port> listen on/connect to port <port> (default 18515)\n");
	fprintf(stdout, " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
	fprintf(stdout, " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
	fprintf(stdout, " -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
	fprintf(stdout, " -s, --service-level <sl> use service level (default 0)\n");
	if (!strcmp(argv0, "./client"))
		fprintf(stdout, " -r, --desired-rate <rate> desired sending rate (default 10, max 40)\n");
	fprintf(stdout, " -h, --help show this help message\n");
}
