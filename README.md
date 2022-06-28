# Memcached over RDMA

For original Memcached readme open README-MEMCACHED.md

This is a fork of Memcached that adds RDMA capabilities. This work was done for a Bachelor Thesis at Vrije Universiteit Amsterdam.
Based on Memcached 1.6.14

## Dependencies
Original Memcached deps:
* libevent - https://www.monkey.org/~provos/libevent/ (libevent-dev)
* libseccomp (optional, experimental, linux) - enables process restrictions for
  better security. Tested only on x86-64 architectures.
* openssl (optional) - enables TLS support. need relatively up to date
  version.

RDMA deps:
* libibverbs
* librdmacm-devel

## Installation
The installation is the same as when installing Memcached from source, the following command should suffice.
Note: you should change memcached-install-dir to whatever directory you want to install your memcached executable to.

```
./configure --prefix=/memcached-install-dir
make && make install
```

If you installed the RDMA dependencies, not problem should arise.

## Usage

The usage is the same as the original Memcached, the only addition is the flag --rdma which will start Memcached using RDMA instead of sockets over TCP or UDP.
Depending from your setup you might have to specify the ip address of your RDMA capable NIC.

Example usage:
memcached --rdma -l 10.149.0.55 -v

## Notes

This software was only tested on the [DAS-5](https://www.cs.vu.nl/das5/) system of the Vrije Universiteit Amsterdam.
The directory das5 contains some utility scripts that will only work on the DAS-5.