#ifndef COMMON_H
#define COMMON_H
//#define DEBUG
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <pthread.h>
#include <assert.h>

#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define DATA_TYPE float
#define MAX_CONCURRENT_WRITES 1024
#define QUEUE_DEPTH_DEFAULT 1024
#define MESSAGE_SIZE (256)
#define BLOCK_SIZE (256)
#define BLOCKS_PER_MESSAGE (MESSAGE_SIZE/BLOCK_SIZE)
#define NUM_QPS 1
#define NUM_THREADS 8
#define NUM_SLOTS (16*NUM_QPS*4) //K*NUM_QPS*num_aggregators
//#define NUM_SLOTS 2
#define DATA_SIZE_PER_THREAD (16*1024*1024)
//#define DATA_SIZE_PER_THREAD (16)
#define DATA_SIZE (DATA_SIZE_PER_THREAD*NUM_THREADS)
#define BITMAP_SIZE_PER_THREAD (DATA_SIZE_PER_THREAD/MESSAGE_SIZE)
#define BITMAP_SIZE (BITMAP_SIZE_PER_THREAD*NUM_THREADS)
#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

/* structure of test parameters */
struct config_t
{
	const char *dev_name; /* IB device name */
	char *server_name;	/* server host names */
	int num_peers;           /* number of servers or clients  */
	char* peer_names[10];   /* name of servers or clients*/
	u_int32_t tcp_port;   /* server TCP port */
	int ib_port;		  /* local IB port to work with */
	int gid_idx;		  /* gid index to use */
	int sl;               /* service level to use */
	bool isServer;        /* server tag*/
};
/* structure to exchange data which is needed to connect the QPs */
struct cm_con_data_t
{
	int remoteId;
	int num_machines;
	uint64_t addr;   /* Buffer address */
	uint32_t rkey;   /* Remote key */
	uint32_t qp_num[10*NUM_QPS*NUM_THREADS]; /* QP number */
	uint16_t lid;	/* LID of the IB port */
	uint8_t gid[16]; /* gid */
} __attribute__((packed));

/* structure of system resources */
struct resources
{
	int myId;
	int threadId;
	struct ibv_device_attr
		device_attr;
	/* Device attributes */
	struct ibv_port_attr port_attr;	/* IB port attributes */
	struct cm_con_data_t remote_props; /* values to connect to remote side */
	struct cm_con_data_t remote_props_array[10]; /* values to connect to remote side */
	struct ibv_context *ib_ctx;		   /* device handle */
	struct ibv_pd *pd;				   /* PD handle */
	struct ibv_comp_channel* event_channel;	 /* Completion event channel, to wait for work completions */
	struct ibv_cq *cq[NUM_THREADS];				   /* CQ handle */
	//struct ibv_qp *qp[NUM_QPS*NUM_THREADS];				   /* QP handle */
	struct ibv_qp **qp;				   /* QP handle */
	struct ibv_mr *mr;				   /* MR handle for buf */
	DATA_TYPE *buf;			    /* memory buffer pointer, used for RDMA and send ops */
	DATA_TYPE *comm_buf;                /* memory buffer pointer, send/recv buffer */
	int *bitmap;
	int sock_status;				           /* TCP socket status */
	int num_socks;
	int num_machines;
	int socks[10];						   /* TCP sockets file descriptor */
	pthread_t cq_poller_thread;        /* thread to poll completion queue */
};
void increment_receive(int threadId);
void increment_send(int threadId);
void show_stat();
int get_workerid_by_qp_num(uint32_t qp_num);
int sock_connect(struct resources *res, struct config_t config);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);
void *poll_cq(struct resources *res);
void poll_cq_main(struct resources *res);
int post_send(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset);
int post_send_server(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset, uint32_t imm, int slot, uint32_t qp_num, int set);
int post_send_client(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset, uint32_t imm, int slot, uint32_t qp_num);
int post_receive(struct resources *res);
int post_receive_server(struct resources *res, int qp_id, uint32_t qp_num);
int post_receive_client(struct resources *res, int slot, uint32_t qp_num);
void resources_init(struct resources *res);
int resources_create(struct resources *res, struct config_t config);
int modify_qp_to_init(struct ibv_qp *qp, struct config_t config);
int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid, struct config_t config);
int modify_qp_to_rts(struct ibv_qp *qp, struct config_t config);
int connect_qp(struct resources *res, struct config_t config);
int resources_destroy(struct resources *res);
void print_config(struct config_t config);
void usage(const char *argv0);

#endif
