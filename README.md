Sparse Collective Communication (OmniReduce)
==================================================================

This is an implementation of OmniReduce with support for RDMA via RoCE.

Requirements
------------

* libibverbs-dev （Tested with MLNX_OFED_LINUX-5.0）

Testing
-------

* Clone and build project:
  ```git clone https://github.com/Phlix1/OmniReduce-RDMA-Demo.git```
  ```cd OmniReduce-RDMA-Demo && make```
* Run tests (n aggregators and m workers):
  - For n aggregators:
    ```./server -g 2 -d mlx5_0 -i 1 -s 2 [worker_1_ip,worker_2_ip,...,worker_m_ip]```
  - For m workers:
    ```./client -g 2 -d mlx5_0 -i 1 -s 2 [agg_1_ip,agg_2_ip,...,agg_n_ip]```

Design overview
---------------

TBD
