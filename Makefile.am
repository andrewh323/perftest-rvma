#
# Copyright (C) 2001-2011 Mellanox Technologies Ltd.  All rights reserved.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# OpenIB.org BSD license below:
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      - Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      - Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials
#        provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# Override to use custom rdma-core headers and libraries
LDFLAGS += -L$(HOME)/src/rdma-core/build/lib -Wl,-rpath=$(HOME)/src/rdma-core/build/lib
CPPFLAGS += -I$(HOME)/src/rdma-core/build/include

ACLOCAL_AMFLAGS= -I m4
AUTOMAKE_OPTIONS= subdir-objects

noinst_LIBRARIES = libperftest.a
libperftest_a_SOURCES = src/get_clock.c src/perftest_communication.c src/perftest_parameters.c src/perftest_resources.c src/perftest_counters.c src/host_memory.c src/mmap_memory.c src/rvma_write.c src/rvma_mailbox_hashmap.c src/rvma_buffer_queue.c src/rvma_common.c src/rvma_socket.c src/indexer.c
noinst_HEADERS = src/get_clock.h src/perftest_communication.h src/perftest_parameters.h src/perftest_resources.h src/perftest_counters.h src/memory.h src/host_memory.h src/mmap_memory.h src/cuda_memory.h src/rocm_memory.h src/neuron_memory.h src/hl_memory.h src/rvma_write.h src/rvma_mailbox_hashmap.h src/rvma_buffer_queue.h src/rvma_common.h src/rvma_socket.h src/indexer.h

if CUDA
libperftest_a_SOURCES += src/cuda_memory.c
endif

if ROCM
libperftest_a_SOURCES += src/rocm_memory.c
endif

if NEURON
libperftest_a_SOURCES += src/neuron_memory.c
endif

if HABANALABS
libperftest_a_SOURCES += src/hl_memory.c
endif

bin_PROGRAMS = ib_write_lat ib_write_bw rsocket_client_lat rsocket_server_lat rsocket_client_bw rsocket_server_bw rvma_mailbox_server rvma_mailbox_client rvsocket_client_stream rvsocket_server_stream rvsocket_server_dgram rvsocket_client_dgram
bin_SCRIPTS = run_perftest_loopback run_perftest_multi_devices

if HAVE_RAW_ETH
libperftest_a_SOURCES += src/raw_ethernet_resources.c
noinst_HEADERS += src/raw_ethernet_resources.h
bin_PROGRAMS += raw_ethernet_bw raw_ethernet_lat raw_ethernet_burst_lat raw_ethernet_fs_rate
else
libperftest_a_SOURCES +=
noinst_HEADERS +=
bin_PROGRAMS +=
endif

if IS_FREEBSD
LIBMLX4= -lmlx4
else
LIBMLX4=
endif

rvsocket_client_dgram_SOURCES = src/rvsocket_client_dgram.c
rvsocket_client_dgram_LDADD = libperftest.a -lrdmacm -libverbs $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rvsocket_server_dgram_SOURCES = src/rvsocket_server_dgram.c
rvsocket_server_dgram_LDADD = libperftest.a -lrdmacm -libverbs $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rvsocket_client_stream_SOURCES = src/rvsocket_client_stream.c
rvsocket_client_stream_LDADD = libperftest.a -lrdmacm -libverbs $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rvsocket_server_stream_SOURCES = src/rvsocket_server_stream.c
rvsocket_server_stream_LDADD = libperftest.a -lrdmacm -libverbs $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rvma_mailbox_server_SOURCES = src/rvma_mailbox_server.c
rvma_mailbox_server_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rvma_mailbox_client_SOURCES = src/rvma_mailbox_client.c
rvma_mailbox_client_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rsocket_client_lat_SOURCES = src/rsocket_client_lat.c
rsocket_client_lat_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rsocket_server_lat_SOURCES = src/rsocket_server_lat.c
rsocket_server_lat_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rsocket_client_bw_SOURCES = src/rsocket_client_bw.C
rsocket_client_bw_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

rsocket_server_bw_SOURCES = src/rsocket_server_bw.C
rsocket_server_bw_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

ib_write_lat_SOURCES = src/write_lat.c
ib_write_lat_LDADD = libperftest.a -lrdmacm $(LIBMATH)  $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

ib_write_bw_SOURCES = src/write_bw.c
ib_write_bw_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

if HAVE_RAW_ETH
raw_ethernet_bw_SOURCES = src/raw_ethernet_send_bw.c
raw_ethernet_bw_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

raw_ethernet_lat_SOURCES = src/raw_ethernet_send_lat.c
raw_ethernet_lat_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

raw_ethernet_burst_lat_SOURCES = src/raw_ethernet_send_burst_lat.c
raw_ethernet_burst_lat_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

raw_ethernet_fs_rate_SOURCES = src/raw_ethernet_fs_rate.c
raw_ethernet_fs_rate_LDADD = libperftest.a -lrdmacm $(LIBMATH) $(LIBMLX4) $(LIBMLX5) $(LIBEFA)

else
raw_ethernet_bw_SOURCES =
raw_ethernet_bw_LDADD =

raw_ethernet_lat_SOURCES =
raw_ethernet_lat_LDADD =

endif