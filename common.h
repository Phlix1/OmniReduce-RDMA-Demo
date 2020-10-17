#ifndef COMMON_H
#define COMMON_H
//#define DEBUG
#include <iostream>
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

#define MAX_CONCURRENT_WRITES 1024
#define QUEUE_DEPTH_DEFAULT 1024
#define MSG "SEND operation "
#define RDMAMSGR "RDMA read operation "
#define RDMAMSGW "RDMA write operation"
#define MSG_SIZE 50*1024*1024
#define MESSAGE_SIZE (1024)
#define NUM_SLOTS 64
#define NUM_QPS 4
#define NUM_THREADS 8
#define DATA_SIZE_PER_THREAD (128*1024*1024)
#define DATA_SIZE (DATA_SIZE_PER_THREAD*NUM_THREADS)
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
	char *server_name;	/* server host name */
	u_int32_t tcp_port;   /* server TCP port */
	int ib_port;		  /* local IB port to work with */
	int gid_idx;		  /* gid index to use */
	int sl;               /* service level to use */
};
/* structure to exchange data which is needed to connect the QPs */
struct cm_con_data_t
{
	uint64_t addr;   /* Buffer address */
	uint32_t rkey;   /* Remote key */
	uint32_t qp_num[NUM_QPS*NUM_THREADS]; /* QP number */
	uint16_t lid;	/* LID of the IB port */
	uint8_t gid[16]; /* gid */
} __attribute__((packed));

/* structure of system resources */
struct resources
{
	int threadId;
	struct ibv_device_attr
		device_attr;
	/* Device attributes */
	struct ibv_port_attr port_attr;	/* IB port attributes */
	struct cm_con_data_t remote_props; /* values to connect to remote side */
	struct ibv_context *ib_ctx;		   /* device handle */
	struct ibv_pd *pd;				   /* PD handle */
	struct ibv_comp_channel* event_channel;	 /* Completion event channel, to wait for work completions */
	struct ibv_cq *cq[NUM_THREADS];				   /* CQ handle */
	struct ibv_qp *qp[NUM_QPS*NUM_THREADS];				   /* QP handle */
	struct ibv_mr *mr;				   /* MR handle for buf */
	char *buf;						   /* memory buffer pointer, used for RDMA and send ops */
	int sock;						   /* TCP socket file descriptor */
	pthread_t cq_poller_thread;        /* thread to poll completion queue */
};

int sock_connect(const char *servername, int port);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);
void *poll_cq(struct resources *res);
void poll_cq_main(struct resources *res);
int post_send(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset);
int post_send_server(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset, uint32_t imm, int slot);
int post_send_client(struct resources *res, ibv_wr_opcode opcode, uint32_t len, uint32_t offset, uint32_t imm, int slot);
int post_receive(struct resources *res);
int post_receive_server(struct resources *res, uint32_t offset, int slot);
int post_receive_client(struct resources *res, uint32_t offset, int slot);
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
