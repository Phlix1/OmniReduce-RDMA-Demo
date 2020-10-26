Sparse Collective Communication (OmniReduce)
==================================================================

This is an implementation of OmniReduce with support for RDMA via RoCE.

Requirements
------------

* libibverbs-dev （Tested with MLNX_OFED_LINUX-5.0）

Design overview
---------------

RDMA connection is established between each worker and each aggregator.
The number of Completion Queue (CQ) for each machine (worker or aggregator) is ```NUM_THREADS``` which is the number of threads created.
The number of Queue Pair (QP) between one worker and one aggregator is controlled by ```NUM_QPS``` and ```NUM_THREADS```.

As each worker connects to all ```n``` aggregators, the number of total queue pairs created in each worker is ```n*NUM_QPS*NUM_THREADS```.
Each aggregator connects to all ```m``` workers, so the number of queue pairs created in each aggregator is ```m*NUM_QPS*NUM_THREADS```.
Figure shows how QPs connect between two workers and one aggregator when ```NUM_QPS``` is 2 and ```NUM_THREADS``` is 1.

<img src="https://user-images.githubusercontent.com/9972418/97232862-02692580-17ef-11eb-92cd-0d2decbf4b19.png"  width="300" />

Testing
-------

* Clone and build project:

  ```git clone https://github.com/Phlix1/OmniReduce-RDMA-Demo.git```
  
  ```cd OmniReduce-RDMA-Demo && make```
  
* Run tests (n aggregators and m workers):
  - For each aggregator:
  
    ```./server -g 2 -d mlx5_0 -i 1 -s 2 [worker_1_ip,worker_2_ip,...,worker_m_ip]```
    
  - For each worker:
  
    ```./client -g 2 -d mlx5_0 -i 1 -s 2 [agg_1_ip,agg_2_ip,...,agg_n_ip]```
  
