/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2005 Hewlett Packard, Inc (Grant Grundler)
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 * Copyright (c) 2024 Ethan Shama, Nathan Kowal, Nicholas Chivaran, Samantha Hawco.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if !defined(__FreeBSD__)
#include <malloc.h>
#endif

#include "get_clock.h"
#include "perftest_parameters.h"
#include "perftest_resources.h"
#include "perftest_communication.h"

/******************************************************************************
 *
 ******************************************************************************/
int main(int argc, char *argv[])
{
	int				ret_parser, i = 0, rc;
	struct report_options		report;
	struct pingpong_context		ctx;
	struct pingpong_dest		*my_dest  = NULL;
	struct pingpong_dest		*rem_dest = NULL;
	struct ibv_device		*ib_dev;
	struct perftest_parameters	user_param;
	struct perftest_comm		user_comm;
	int rdma_cm_flow_destroyed = 0;

    struct timeval              rdma_start_time;
    struct timeval              rdma_cold_start_time;
    struct timeval              rdma_end_time;

	/* init default values to user's parameters */
	memset(&ctx,0,sizeof(struct pingpong_context));
	memset(&user_param, 0, sizeof(struct perftest_parameters));
	memset(&user_comm,0,sizeof(struct perftest_comm));

	user_param.verb    = WRITE_IMM;
	user_param.tst     = LAT;
	user_param.r_flag  = &report;
	strncpy(user_param.version, VERSION, sizeof(user_param.version));

	/* Configure the parameters values according to user arguments or defalut values. */
	ret_parser = parser(&user_param,argv,argc);
	if (ret_parser) {
		if (ret_parser != VERSION_EXIT && ret_parser != HELP_EXIT)
			fprintf(stderr," Parser function exited with Error\n");
		goto return_error;
	}

    /*Force it to only do 1 iteration so we can message cold start time */
    user_param.iters = 1;

	/* In case of ib_write_lat, PCI relaxed ordering should be disabled since we're polling for data change
	 * of last packet so in case of relaxed odering we might get the last packet in wrong order thus the test
	 * would be incorrect
	 */
	user_param.disable_pcir = 1;

	if (user_param.use_xrc || user_param.connection_type == DC) {
		user_param.num_of_qps *= 2;
	}

    struct timeval temp;
    gettimeofday(&temp, NULL);
    rdma_cold_start_time.tv_sec = temp.tv_sec;
    rdma_cold_start_time.tv_usec = temp.tv_usec;
	/* Finding the IB device selected (or defalut if no selected). */
	ib_dev = ctx_find_dev(&user_param.ib_devname);
	if (!ib_dev) {
		fprintf(stderr," Unable to find the Infiniband/RoCE device\n");
		goto return_error;
	}

	/* Getting the relevant context from the device */
	ctx.context = ctx_open_device(ib_dev, &user_param);
	if (!ctx.context) {
		fprintf(stderr, " Couldn't get context for the device\n");
		goto free_devname;
	}

	/* Verify user parameters that require the device context,
	 * the function will print the relevent error info. */
	if (verify_params_with_device_context(ctx.context, &user_param))
	{
		fprintf(stderr, " Couldn't get context for the device\n");
		goto free_devname;
	}

	/* See if link type is valid and supported. */
	if (check_link(ctx.context,&user_param)) {
		fprintf(stderr, " Couldn't get context for the device\n");
		goto free_devname;
	}

	/* copy the relevant user parameters to the comm struct + creating rdma_cm resources. */
	if (create_comm_struct(&user_comm,&user_param)) {
		fprintf(stderr," Unable to create RDMA_CM resources\n");
		goto free_devname;
	}

	if (user_param.output == FULL_VERBOSITY && user_param.machine == SERVER) {
		printf("\n************************************\n");
		printf("* Waiting for client to connect... *\n");
		printf("************************************\n");
	}

	/* Initialize the connection and print the local data. */
	if (establish_connection(&user_comm)) {
		fprintf(stderr," Unable to init the socket connection\n");
		dealloc_comm_struct(&user_comm,&user_param);
		goto free_devname;
	}

	exchange_versions(&user_comm, &user_param);
	check_version_compatibility(&user_param);
	check_sys_data(&user_comm, &user_param);

	/* See if MTU is valid and supported. */
	if (check_mtu(ctx.context,&user_param, &user_comm)) {
		fprintf(stderr, " Couldn't get context for the device\n");
		dealloc_comm_struct(&user_comm,&user_param);
		goto free_devname;
	}

	MAIN_ALLOC(my_dest , struct pingpong_dest , user_param.num_of_qps , free_rdma_params);
	memset(my_dest, 0, sizeof(struct pingpong_dest)*user_param.num_of_qps);
	MAIN_ALLOC(rem_dest , struct pingpong_dest , user_param.num_of_qps , free_my_dest);
	memset(rem_dest, 0, sizeof(struct pingpong_dest)*user_param.num_of_qps);

	/* Allocating arrays needed for the test. */
	if(alloc_ctx(&ctx,&user_param)){
		fprintf(stderr, "Couldn't allocate context\n");
		goto free_mem;
	}

	/* Create RDMA CM resources and connect through CM. */
	if (user_param.work_rdma_cm == ON) {
		rc = create_rdma_cm_connection(&ctx, &user_param, &user_comm,
			my_dest, rem_dest);
		if (rc) {
			fprintf(stderr,
				"Failed to create RDMA CM connection with resources.\n");
			dealloc_ctx(&ctx, &user_param);
			goto free_mem;
		}
	} else {
		/* create all the basic IB resources (data buffer, PD, MR, CQ and events channel) */
		if (ctx_init(&ctx,&user_param)) {
			fprintf(stderr, " Couldn't create IB resources\n");
			dealloc_ctx(&ctx, &user_param);
			goto free_mem;
		}
	}

    gettimeofday(&temp, NULL);
    rdma_start_time.tv_sec = temp.tv_sec;
    rdma_start_time.tv_usec = temp.tv_usec;
	/* Set up the Connection. */
	if (set_up_connection(&ctx,&user_param,my_dest)) {
		fprintf(stderr," Unable to set up socket connection\n");
		goto destroy_context;
	}

	/* Print basic test information. */
	ctx_print_test_info(&user_param);

	/* shaking hands and gather the other side info. */
	if (ctx_hand_shake(&user_comm,my_dest,rem_dest)) {
		fprintf(stderr,"Failed to exchange data between server and clients\n");
		goto destroy_context;
	}

	for (i=0; i < user_param.num_of_qps; i++) {
		/* shaking hands and gather the other side info. */
		if (ctx_hand_shake(&user_comm,&my_dest[i],&rem_dest[i])) {
			fprintf(stderr,"Failed to exchange data between server and clients\n");
			goto destroy_context;
		}

	}

	if (user_param.work_rdma_cm == OFF) {
		if (ctx_check_gid_compatibility(&my_dest[0], &rem_dest[0])) {
			fprintf(stderr,"\n Found Incompatibility issue with GID types.\n");
			fprintf(stderr," Please Try to use a different IP version.\n\n");
			goto destroy_context;
		}
	}

	if (user_param.work_rdma_cm == OFF) {
		if (ctx_connect(&ctx,rem_dest,&user_param,my_dest)) {
			fprintf(stderr," Unable to Connect the HCA's through the link\n");
			goto destroy_context;
		}
	}

	if (user_param.connection_type == DC)
	{
		/* Set up connection one more time to send qpn properly for DC */
		if (set_up_connection(&ctx,&user_param,my_dest)) {
			fprintf(stderr," Unable to set up socket connection\n");
			goto destroy_context;
		}
	}

	/* Print this machine QP information */
	for (i=0; i < user_param.num_of_qps; i++)
		ctx_print_pingpong_data(&my_dest[i],&user_comm);

	user_comm.rdma_params->side = REMOTE;

	for (i=0; i < user_param.num_of_qps; i++) {
		if (ctx_hand_shake(&user_comm,&my_dest[i],&rem_dest[i])) {
			fprintf(stderr," Failed to exchange data between server and clients\n");
			goto destroy_context;
		}

		ctx_print_pingpong_data(&rem_dest[i],&user_comm);
	}

	/* An additional handshake is required after moving qp to RTR. */
	if (ctx_hand_shake(&user_comm,my_dest,rem_dest)) {
		fprintf(stderr,"Failed to exchange data between server and clients\n");
		goto destroy_context;
	}

	ctx_set_send_wqes(&ctx,&user_param,rem_dest);

	if (user_param.output == FULL_VERBOSITY) {
		printf(RESULT_LINE);
		printf("%s",(user_param.test_type == ITERATIONS) ? RESULT_FMT_LAT : RESULT_FMT_LAT_DUR);
		printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
	}

	if (user_param.test_method == RUN_ALL) {

		for (i = 1; i < 24 ; ++i) {
			user_param.size = (uint64_t)1 << i;

			if (user_param.verb == WRITE_IMM) {
				/* Post receive recv_wqes fo current message size */
				if (ctx_set_recv_wqes(&ctx,&user_param)) {
					fprintf(stderr," Failed to post receive recv_wqes\n");
					goto free_mem;
				}

				/* Sync between the client and server so the client won't send packets
				 * Before the server has posted his receive wqes (in UC/UD it will result in a deadlock).
				 */

				if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
					fprintf(stderr,"Failed to exchange data between server and clients\n");
					goto free_mem;
				}

				if(run_iter_lat_write_imm(&ctx,&user_param)) {
					fprintf(stderr,"Test exited with Error\n");
					goto free_mem;
				}
			} else {
				if(run_iter_lat_write(&ctx,&user_param)) {
					fprintf(stderr,"Test exited with Error\n");
					goto free_mem;
				}
			}

			user_param.test_type == ITERATIONS ? print_report_lat(&user_param) : print_report_lat_duration(&user_param);
		}

	} else {

		if (user_param.verb == WRITE_IMM) {
			/* Post recevie recv_wqes fo current message size */
			if (ctx_set_recv_wqes(&ctx,&user_param)) {
				fprintf(stderr," Failed to post receive recv_wqes\n");
				goto free_mem;
			}

			/* Sync between the client and server so the client won't send packets
			* Before the server has posted his receive wqes (in UC/UD it will result in a deadlock).
			*/

			if (ctx_hand_shake(&user_comm,my_dest,rem_dest)) {
				fprintf(stderr,"Failed to exchange data between server and clients\n");
				goto free_mem;
			}

			if(run_iter_lat_write_imm(&ctx,&user_param)) {
				fprintf(stderr,"Test exited with Error\n");
				goto free_mem;
			}
		} else {
			if(run_iter_lat_write(&ctx,&user_param)) {
				fprintf(stderr,"Test exited with Error\n");
				goto free_mem;
			}
		}

        // Dont need these res printed as they rely on the interations which is disabled
		//user_param.test_type == ITERATIONS ? print_report_lat(&user_param) : print_report_lat_duration(&user_param);
	}

    gettimeofday(&temp, NULL);
    rdma_end_time.tv_sec = temp.tv_sec;
    rdma_end_time.tv_usec = temp.tv_usec;

	if (user_param.output == FULL_VERBOSITY) {
		printf(RESULT_LINE);
	}

    if(user_param.machine == CLIENT){
        printf("RVMA Testing Results for machine type: CLIENT\n");
        long mtime, seconds, useconds;
        seconds = rdma_end_time.tv_sec - rdma_cold_start_time.tv_sec;
        useconds = rdma_end_time.tv_usec - rdma_cold_start_time.tv_usec;
        mtime = ((seconds) * 1000000L + useconds);
        printf("Total RDMA Cold Start Elapsed time: %ld microseconds\n", mtime);

        seconds = rdma_end_time.tv_sec - rdma_start_time.tv_sec;
        useconds = rdma_end_time.tv_usec - rdma_start_time.tv_usec;
        mtime = ((seconds) * 1000000L + useconds);
        printf("Total RDMA Elapsed time: %ld microseconds\n", mtime);

        seconds = ctx.rvma_end_time.tv_sec - ctx.rvma_start_time.tv_sec;
        useconds = ctx.rvma_end_time.tv_usec - ctx.rvma_start_time.tv_usec;
        mtime = ((seconds) * 1000000L + useconds);
        printf("Total RVMA Elapsed time: %ld microseconds\n", mtime);

    }else{
        printf("RVMA Testing Results for machine type: SERVER\n");
        printf("Results for the mailbox with Virtual Address of: %d\n", *ctx.rvma_vaddr);
        printf("The RVMA notification Buff Ptr flag is: %d\n", *(int *) (*ctx.rvma_notifBuffPtrAddr));
        printf("The RVMA notification Len Ptr indicates: %d bytes written\n", *(int *) (*ctx.rvma_notifLenPtrAddr));

        RVMA_Mailbox *mailbox = searchHashmap(ctx.rvma_window->hashMapPtr, ctx.rvma_vaddr);
        printf("The VAddr mailbox has this many retired queue entries: %d\n", mailbox->retiredBufferQueue->size);
        printf("The VAddr mailbox has this many queue entries: %d\n", mailbox->bufferQueue->size);

        /*
         * Testing Mailbox Contents
         * In the latency test we know the buffer will not be retired so we examine the regular buffer queue
         */
        RVMA_Status verifiedResult = rvmaCheckBufferQueue(mailbox->bufferQueue, user_param.tst);

        if (verifiedResult == RVMA_SUCCESS){
            printf("The results in the buffer were verified to be correct\n");
        }
        else if (verifiedResult == RVMA_ERROR){
            printf("The results could not be verified due to issue with variables passed into the function\n");
        }
        else if (verifiedResult == RVMA_FAILURE){
            printf("The results in the buffer were verified to be incorrect\n");
        }else{
			printf("rvmaCheckBufferQueue returned an unexpected value\n");
		}

    }

	if (user_param.work_rdma_cm == ON) {
		if (destroy_ctx(&ctx,&user_param)) {
			fprintf(stderr, "Failed to destroy resources\n");
			goto destroy_cm_context;
		}

		user_comm.rdma_params->work_rdma_cm = OFF;
		free(rem_dest);
		free(my_dest);
		free(user_param.ib_devname);
		if(destroy_ctx(user_comm.rdma_ctx, user_comm.rdma_params)) {
			free(user_comm.rdma_ctx);
			free(user_comm.rdma_params);
			return FAILURE;
		}
		free(user_comm.rdma_ctx);
		free(user_comm.rdma_params);
		return SUCCESS;
	}

	free(rem_dest);
	free(my_dest);
	free(user_param.ib_devname);

	if(destroy_ctx(&ctx, &user_param)){
		free(user_comm.rdma_params);
		return FAILURE;
	}
	free(user_comm.rdma_params);
	return SUCCESS;

destroy_context:
	if (destroy_ctx(&ctx,&user_param))
		fprintf(stderr, "Failed to destroy resources\n");
destroy_cm_context:
	if (user_param.work_rdma_cm == ON) {
		rdma_cm_flow_destroyed = 1;
		user_comm.rdma_params->work_rdma_cm = OFF;
		destroy_ctx(user_comm.rdma_ctx,user_comm.rdma_params);
	}
free_mem:
	free(rem_dest);
free_my_dest:
	free(my_dest);
free_rdma_params:
	if (user_param.use_rdma_cm == ON && rdma_cm_flow_destroyed == 0)
		dealloc_comm_struct(&user_comm, &user_param);
	else {
		if(user_param.use_rdma_cm == ON)
			free(user_comm.rdma_ctx);
		free(user_comm.rdma_params);
	}
free_devname:
	free(user_param.ib_devname);
return_error:
	//coverity[leaked_storage]
	return FAILURE;
}
