/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
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

#include "perftest_parameters.h"
#include "perftest_resources.h"
#include "perftest_communication.h"

/******************************************************************************
 ******************************************************************************/

int main(int argc, char *argv[])
{
	int				ret_parser, i = 0, rc;
	struct ibv_device		*ib_dev = NULL;
	struct pingpong_context		ctx;
	struct pingpong_dest		*my_dest,*rem_dest;
	struct perftest_parameters	user_param;
	struct perftest_comm		user_comm;
	struct bw_report_data		my_bw_rep, rem_bw_rep;
	int rdma_cm_flow_destroyed = 0;

    struct timeval              rdma_start_time;
    struct timeval              rdma_cold_start_time;
    struct timeval              rdma_end_time;

	/* init default values to user's parameters */
	memset(&user_param,0,sizeof(struct perftest_parameters));
	memset(&user_comm,0,sizeof(struct perftest_comm));
	memset(&ctx,0,sizeof(struct pingpong_context));

	user_param.verb    = WRITE_IMM;
	user_param.tst     = BW;
	strncpy(user_param.version, VERSION, sizeof(user_param.version));

	/* Configure the parameters values according to user arguments or default values. */
	ret_parser = parser(&user_param,argv,argc);
	if (ret_parser) {
		if (ret_parser != VERSION_EXIT && ret_parser != HELP_EXIT)
			fprintf(stderr," Parser function exited with Error\n");
		goto return_error;
	}

	if((user_param.connection_type == DC || user_param.use_xrc) && user_param.duplex) {
		user_param.num_of_qps *= 2;
	}

    struct timeval temp;
    gettimeofday(&temp, NULL);
    rdma_cold_start_time.tv_sec = temp.tv_sec;
    rdma_cold_start_time.tv_usec = temp.tv_usec;

	/* Finding the IB device selected (or default if none is selected). */
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
	sleep(1);
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
		if (ctx_init(&ctx, &user_param)) {
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

	for (i=0; i < user_param.num_of_qps; i++) {

		if (ctx_hand_shake(&user_comm,&my_dest[i],&rem_dest[i])) {
			fprintf(stderr," Failed to exchange data between server and clients\n");
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
		if (set_up_connection(&ctx, &user_param, my_dest))
		{
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
	if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
		fprintf(stderr," Failed to exchange data between server and clients\n");
		goto destroy_context;
	}

	if (user_param.output == FULL_VERBOSITY) {
		if (user_param.report_per_port) {
			printf(RESULT_LINE_PER_PORT);
			printf((user_param.report_fmt == MBS ? RESULT_FMT_PER_PORT : RESULT_FMT_G_PER_PORT));
		}
		else {
			printf(RESULT_LINE);
			printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
		}

		printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
	}

	/* For half duplex write tests, server just waits for client to exit */
	if (user_param.machine == SERVER && user_param.verb == WRITE && !user_param.duplex) {
		if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
			fprintf(stderr," Failed to exchange data between server and clients\n");
			goto free_mem;
		}

		xchg_bw_reports(&user_comm, &my_bw_rep,&rem_bw_rep,atof(user_param.rem_version));
		print_full_bw_report(&user_param, &rem_bw_rep, NULL);
		if (ctx_close_connection(&user_comm,&my_dest[0],&rem_dest[0])) {
			fprintf(stderr,"Failed to close connection between server and client\n");
			goto free_mem;
		}

		if (user_param.output == FULL_VERBOSITY) {
			if (user_param.report_per_port)
				printf(RESULT_LINE_PER_PORT);
			else
				printf(RESULT_LINE);
		}

		if (user_param.work_rdma_cm == ON) {
			if (destroy_ctx(&ctx,&user_param)) {
				fprintf(stderr, "Failed to destroy resources\n");
				goto destroy_cm_context;
			}
			user_comm.rdma_params->work_rdma_cm = OFF;
			free(my_dest);
			free(rem_dest);
			free(user_param.ib_devname);
			if(destroy_ctx(user_comm.rdma_ctx, user_comm.rdma_params)) {
				free(user_comm.rdma_params);
				free(user_comm.rdma_ctx);
				return FAILURE;
			}
			free(user_comm.rdma_params);
			free(user_comm.rdma_ctx);
			return SUCCESS;
		}

		free(my_dest);
		free(rem_dest);
		free(user_param.ib_devname);
		if(destroy_ctx(&ctx, &user_param)) {
			free(user_comm.rdma_params);
			return FAILURE;
		}
		free(user_comm.rdma_params);
		return SUCCESS;
	}

	if (user_param.test_method == RUN_ALL) {

		for (i = 1; i < 24 ; ++i) {

			user_param.size = (uint64_t)1 << i;

			if (user_param.machine == CLIENT || user_param.duplex)
				ctx_set_send_wqes(&ctx,&user_param,rem_dest);

			if (user_param.verb == WRITE_IMM && (user_param.machine == SERVER || user_param.duplex)) {
				if (ctx_set_recv_wqes(&ctx,&user_param)) {
					fprintf(stderr," Failed to post receive recv_wqes\n");
					goto free_mem;
				}
			}

			if (user_param.perform_warm_up) {

				if (user_param.verb == WRITE_IMM) {
					fprintf(stderr, "Warm up not supported for WRITE_IMM verb.\n");
					fprintf(stderr, "Skipping\n");
				} else if(perform_warm_up(&ctx, &user_param)) {
					fprintf(stderr, "Problems with warm up\n");
					goto free_mem;
				}
			}

			if(user_param.duplex || user_param.verb == WRITE_IMM) {
				if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
					fprintf(stderr,"Failed to sync between server and client between different msg sizes\n");
					goto free_mem;
				}
			}

			if (user_param.duplex && user_param.verb == WRITE_IMM) {

				if(run_iter_bi(&ctx,&user_param)){
					fprintf(stderr," Failed to complete run_iter_bi function successfully\n");
					goto free_mem;
				}

			} else if (user_param.machine == CLIENT || user_param.verb != WRITE_IMM) {

				if(run_iter_bw(&ctx,&user_param)) {
					fprintf(stderr," Failed to complete run_iter_bw function successfully\n");
					goto free_mem;
				}

			} else if (user_param.machine == SERVER) {

				if(run_iter_bw_server(&ctx,&user_param)) {
					fprintf(stderr," Failed to complete run_iter_bw_server function successfully\n");
					goto free_mem;
				}
			}

			if (user_param.verb == WRITE_IMM || (user_param.duplex && (atof(user_param.version) >= 4.6))) {
				if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
					fprintf(stderr,"Failed to sync between server and client between different msg sizes\n");
					goto free_mem;
				}
			}

			print_report_bw(&user_param,&my_bw_rep);

			if (user_param.duplex && (user_param.verb != WRITE_IMM || user_param.test_type != DURATION)) {
				xchg_bw_reports(&user_comm, &my_bw_rep,&rem_bw_rep,atof(user_param.rem_version));
				print_full_bw_report(&user_param, &my_bw_rep, &rem_bw_rep);
			}
		}

	} else if (user_param.test_method == RUN_REGULAR) {

		if (user_param.machine == CLIENT || user_param.duplex)
            // Function used to set up the work requets, these are the messages which are sent to the server. IP, VADDR, Payload, etc
			ctx_set_send_wqes(&ctx,&user_param,rem_dest);

		if (user_param.verb == WRITE_IMM && (user_param.machine == SERVER || user_param.duplex)) {
			if (ctx_set_recv_wqes(&ctx,&user_param)) {
				fprintf(stderr," Failed to post receive recv_wqes\n");
				goto free_mem;
			}
		}

		if (user_param.verb != SEND && user_param.verb != WRITE_IMM) {

			if (user_param.perform_warm_up) {
				if(perform_warm_up(&ctx, &user_param)) {
					fprintf(stderr, "Problems with warm up\n");
					goto free_mem;
				}
			}
		}

		if(user_param.duplex || user_param.verb == WRITE_IMM) {
			if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
				fprintf(stderr,"Failed to sync between server and client between different msg sizes\n");
				goto free_mem;
			}
		}

		if (user_param.duplex && user_param.verb == WRITE_IMM) {

			if(run_iter_bi(&ctx,&user_param)){
				fprintf(stderr," Failed to complete run_iter_bi function successfully\n");
				goto free_mem;
			}

		} else if (user_param.machine == CLIENT || user_param.verb != WRITE_IMM) {
            /**
             *  This is where the actual sending happens
             * **/
			if(run_iter_bw(&ctx,&user_param)) {
				fprintf(stderr," Failed to complete run_iter_bw function successfully\n");
				goto free_mem;
			}

		} else if (user_param.machine == SERVER) {

			if(run_iter_bw_server(&ctx,&user_param)) {
				fprintf(stderr," Failed to complete run_iter_bw_server function successfully\n");
				goto free_mem;
			}
		}

        gettimeofday(&temp, NULL);
        rdma_end_time.tv_sec = temp.tv_sec;
        rdma_end_time.tv_usec = temp.tv_usec;

		print_report_bw(&user_param,&my_bw_rep);

		if (user_param.duplex && (user_param.verb != WRITE_IMM || user_param.test_type != DURATION)) {
			xchg_bw_reports(&user_comm, &my_bw_rep,&rem_bw_rep,atof(user_param.rem_version));
			print_full_bw_report(&user_param, &my_bw_rep, &rem_bw_rep);
		}

		if (user_param.report_both && user_param.duplex) {
			printf(RESULT_LINE);
			printf("\n Local results: \n");
			printf(RESULT_LINE);
			printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
			printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
			print_full_bw_report(&user_param, &my_bw_rep, NULL);
			printf(RESULT_LINE);

			printf("\n Remote results: \n");
			printf(RESULT_LINE);
			printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
			printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
			print_full_bw_report(&user_param, &rem_bw_rep, NULL);
		}
	} else if (user_param.test_method == RUN_INFINITELY) {

		if (user_param.machine == CLIENT || user_param.duplex)
			ctx_set_send_wqes(&ctx,&user_param,rem_dest);

		else if (user_param.machine == SERVER && user_param.verb == WRITE_IMM) {
			if (ctx_set_recv_wqes(&ctx,&user_param)) {
				fprintf(stderr," Failed to post receive recv_wqes\n");
				goto free_mem;
			}
		}

		if (user_param.verb == WRITE_IMM) {
			if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
				fprintf(stderr,"Failed to exchange data between server and clients\n");
				goto free_mem;
			}
		}

		if (user_param.machine == CLIENT || user_param.verb == WRITE) {
			if(run_iter_bw_infinitely(&ctx,&user_param)) {
				fprintf(stderr," Error occurred while running infinitely! aborting ...\n");
				goto free_mem;
			}
		} else if (user_param.machine == SERVER && user_param.verb == WRITE_IMM) {
			if(run_iter_bw_infinitely_server(&ctx,&user_param)) {
				fprintf(stderr," Error occurred while running infinitely on server! aborting ...\n");
				goto free_mem;
			}
		}
	}

	if (user_param.output == FULL_VERBOSITY) {
		if (user_param.report_per_port)
			printf(RESULT_LINE_PER_PORT);
		else
			printf(RESULT_LINE);
	}

	/* For half duplex write tests, server just waits for client to exit */
	if (user_param.machine == CLIENT && user_param.verb == WRITE && !user_param.duplex) {
		if (ctx_hand_shake(&user_comm,&my_dest[0],&rem_dest[0])) {
			fprintf(stderr," Failed to exchange data between server and clients\n");
			goto free_mem;
		}

		xchg_bw_reports(&user_comm, &my_bw_rep,&rem_bw_rep,atof(user_param.rem_version));
	}


    printf(RESULT_LINE);
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
    }
    else{
        printf("RVMA Testing Results for machine type: SERVER\n");
        printf("The total number of bytes transferred was: %llu bytes (Most is repeats and throw away)\n", ctx.buff_size*user_param.iters/2);
        printf("Results for the mailbox with Virtual Address of: %d\n", *ctx.rvma_vaddr);
        printf("The RVMA notification Buff Ptr flag is: %d\n", *(int *) (*ctx.rvma_notifBuffPtrAddr));
        printf("The RVMA notification Len Ptr indicates: %d bytes written\n", *(int *) (*ctx.rvma_notifLenPtrAddr));

        RVMA_Mailbox *mailbox = searchHashmap(ctx.rvma_window->hashMapPtr, ctx.rvma_vaddr);
        printf("The VAddr mailbox has this many retired queue entries: %d\n", mailbox->retiredBufferQueue->size);
        printf("The VAddr mailbox has this many queue entries: %d\n", mailbox->bufferQueue->size);

        /*
         * Testing Mailbox Contents
         * In the bandwidth test we know the buffer will be retired so we examine the retired buffer queue
         */
        RVMA_Status verifiedResult = rvmaCheckBufferQueue(mailbox->retiredBufferQueue, user_param.tst, user_param.size);

        if (verifiedResult == RVMA_SUCCESS){
            printf("The results in the retired buffer were verified to be correct\n");
        }
        else if (verifiedResult == RVMA_ERROR){
            printf("The results could not be verified due to issue with variables passed into the function\n");
        }
        else{
            printf("The results in the retired buffer were verified to be incorrect\n");
        }
    }

	/* Closing connection. */
	if (ctx_close_connection(&user_comm,&my_dest[0],&rem_dest[0])) {
		fprintf(stderr,"Failed to close connection between server and client\n");
		goto free_mem;
	}

	if (!user_param.is_bw_limit_passed && (user_param.is_limit_bw == ON ) ) {
		fprintf(stderr,"Error: BW result is below bw limit\n");
		goto destroy_context;
	}

	if (!user_param.is_msgrate_limit_passed && (user_param.is_limit_bw == ON )) {
		fprintf(stderr,"Error: Msg rate  is below msg_rate limit\n");
		goto destroy_context;
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
			free(user_comm.rdma_params);
			free(user_comm.rdma_ctx);
			return FAILURE;
		}
		free(user_comm.rdma_params);
		free(user_comm.rdma_ctx);
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