/*
 * emb6 is licensed under the 3-clause BSD license. This license gives everyone
 * the right to use and distribute the code, either in binary or source code
 * format, as long as the copyright license is retained in the source code.
 *
 * The emb6 is derived from the Contiki OS platform with the explicit approval
 * from Adam Dunkels. However, emb6 is made independent from the OS through the
 * removal of protothreads. In addition, APIs are made more flexible to gain
 * more adaptivity during run-time.
 *
 * The license text is:
 *
 * Copyright (c) 2015,
 * Hochschule Offenburg, University of Applied Sciences
 * Laboratory Embedded Systems and Communications Electronics.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*============================================================================*/
/*!
    \file   mle_management.c

    \author Nidhal Mars <nidhal.mars@hs-offenburg.de>

    \brief  Mesh Link Establishment protocol

  	\version  0.1
 */
/*==============================================================================
                                 INCLUDE FILES
 =============================================================================*/

#include "emb6.h"
#include "mle_management.h"
#include "thrd-adv.h"
#include "thrd-partition.h"
#include "thrd-iface.h"
#include "thrd-router-id.h"
#include "thrd-send-adv.h"
#include "linkaddr.h"
#include "thrd-route.h"

#define		DEBUG		DEBUG_PRINT
#include "uip-debug.h"	// For debugging terminal output.

#define     LOGGER_ENABLE                 LOGGER_MLE
#include    "logger.h"



/*==============================================================================
                                     MACROS
 =============================================================================*/

#define routerMask(scanMask)			(BIT_CHECK(scanMask,7))
#define ReedMask(scanMask)				(BIT_CHECK(scanMask,6))
#define get_rssi()		           		30 //sicslowpan_get_last_rssi()

/*==============================================================================
                          LOCAL VARIABLE DECLARATIONS
 =============================================================================*/

static st_mle_node_t MyNode;
static uip_ipaddr_t                 s_destAddr;
struct ctimer                       c_mle_Timer;
struct ctimer                       parent_Timer;
static mle_cmd_t 					cmd; // command buffer
static mle_param_t					param;
static mle_neighbor_t*				parent;
static mle_neighbor_t* 				child;
static thrd_rdb_link_t*				nb;



/*==============================================================================
                             LOCAL FUNCTIONS DECLARATIONS
==============================================================================*/

static  uint8_t      isActiveRouter(uint16_t srcAdd);
static  uint8_t      mapRSSI(uint8_t rssi );
static  uint8_t      calculate_two_way_LQ(uint8_t my_rssi , uint8_t rec_rssi);
static  uint8_t      send_mle_parent_request(uint8_t R, uint8_t E);
static  uint8_t      send_mle_childID_request(uip_ipaddr_t *dest_addr);
static  void         time_out_remove_child(void *ptr);
static  void         check_child_state(void *ptr);
static  void         reply_for_mle_parent_request(void *ptr);
static  uint16_t     assign_address16(uint8_t id);
static  void         mle_join_process(void *ptr);
static  uint8_t      send_mle_child_update(uip_ipaddr_t *dest_addr);
static  uint8_t      send_mle_child_update_response(uip_ipaddr_t *dest_addr);
static  uint8_t      send_mle_link_request(void);
static  void         reply_for_mle_link_request(void *ptr);
static  void         mle_synchro_process(void *ptr);
static  void         mle_keep_alive(void *ptr);
static  void         _mle_process_incoming_msg(struct udp_socket *c, void *ptr, const uip_ipaddr_t *source_addr, uint16_t source_port,
		const uip_ipaddr_t *dest_addr, uint16_t dest_port, const uint8_t *data, uint16_t datalen);
static  uint8_t      mle_init_udp(void);
static  uint8_t      mle_send_msg(mle_cmd_t* cmd,  uip_ipaddr_t *dest_addr);

/*==============================================================================
								LOCAL FUNCTION
 =============================================================================*/



/**
 * \brief check from the address if the device is an active router
 *
 * \param  srcAdd       the address of the device to check
 *
 * \return 1    OK.
 * \return 0   FAIL.
 *
 */
static uint8_t 			isActiveRouter(uint16_t srcAdd)
{
	for(uint8_t i=0; i<9;i++)
	{
		if(BIT_CHECK(srcAdd,i))
			return 0;
	}
	return 1 ;
}


/************************ link calculation functions ****************************/

/**
 * \brief mapping RSSI
 *
 * \param  rssi     Received Signal Strength Indication value
 *
 * \return corresponding value
 *
 */
static uint8_t mapRSSI(uint8_t rssi )
{
	if ( rssi > 20)
		return 3;
	else if ( (rssi > 10 )
			&& (rssi <= 20  ))
		return 2;
	else if ( (rssi > 2 )
			&& (rssi <= 10  ))
		return 1;
	else
		return 0;
}



/**
 * \brief Calculate the two-way link quality
 *
 * \param  my_rssi      my rssi (received on link margin TLV)
 * \param  rec_rssi     received rssi (calculated by the transceiver)
 *
 * \return two-way link quality
 *
 */
static uint8_t calculate_two_way_LQ(uint8_t my_rssi , uint8_t rec_rssi)
{
	if (my_rssi<rec_rssi)
		return mapRSSI(my_rssi);
	else
		return mapRSSI(rec_rssi);
}

/************************ join process functions ****************************/


/**
 * \brief  send MLE Parent Request message
 *
 * \param  R     Router: Active Routers MUST respond
 * \param  E     End device: REEDs that are currently acting as Children MUST respond
 *
 * \return
 *       -  1 success
 *       -  0 error
 */

static uint8_t send_mle_parent_request(uint8_t R, uint8_t E)
{
	mle_init_cmd(&cmd,PARENT_REQUEST);
	add_mode_RSDN_to_cmd(&cmd, MyNode.rx_on_when_idle, 0, IS_FFD, 0);
	MyNode.challenge=add_rand_challenge_to_cmd(&cmd);
	add_scan_mask_to_cmd(&cmd,R,E);
	add_version_to_cmd(&cmd);
	uip_create_linklocal_allrouters_mcast(&s_destAddr);
	if(mle_send_msg(&cmd, &s_destAddr))
		return 1;
	else
		return 0;
}

/**
 * \brief  send child id request
 *
 * \param  dest_addr	  pointer to the dest address
 *
 * \return
 *       -  1 success
 *       -  0 error
 */
static uint8_t send_mle_childID_request(uip_ipaddr_t *dest_addr)
{
	mle_init_cmd(&cmd,CHILD_ID_REQUEST);
	add_response32_to_cmd(&cmd, parent->challenge);
	// link-layer frame counter tlv
	// MLE Frame Counter tlv
	add_mode_RSDN_to_cmd(&cmd, MyNode.rx_on_when_idle, 0, IS_FFD, 0);
	add_time_out_to_cmd(&cmd,MyNode.timeOut);
	add_version_to_cmd(&cmd);
	if(mle_send_msg(&cmd, dest_addr))
		return 1;
	else
		return 0;
}


/**
 * \brief  remove a child after time out (this function is triggered by child's timer)
 *
 * \param  ptr	  pointer to the child to remove
 */
static void time_out_remove_child(void *ptr)
{
	mle_neighbor_t*	 nb_to_rem;
	nb_to_rem= (mle_neighbor_t*) ptr;
	mle_rm_child(nb_to_rem);
	MyNode.childs_counter--;
	LOG_RAW(ANSI_COLOR_RED "Time out : child removed\n\r"ANSI_COLOR_RESET);
}


/**
 * \brief  remove a child after time out (this function is triggered by child's timer)
 *
 * \param  ptr	  pointer to the child to remove
 */
static void check_child_state(void *ptr)
{
	uint8_t* child_id;
	child_id= (uint8_t*) ptr;
	child=mle_find_child(*child_id);

	if (child->state==PENDING)
	{
		mle_rm_child(child);
		MyNode.childs_counter--;
		LOG_RAW(ANSI_COLOR_RED "Time out : child removed\n\r"ANSI_COLOR_RESET);
	}
	else
	{
		LOG_RAW("Child linked\n\r");
		/* trigger the timer to count the time out  */
		ctimer_set(&child->timer, child->time_out * bsp_getTRes() , time_out_remove_child, (void *) &child );
		LOG_RAW("Child time out is : %i\n\r" , child->time_out);
	}

}



/**
 * \brief  replying for MLE parent request by unicast parent response
 *
 * \param  ptr	  pointer to the child id
 */
static void reply_for_mle_parent_request(void *ptr)
{

	uint8_t* child_id;
	child_id= (uint8_t*) ptr;

	if(MyNode.OpMode!= NOT_LINKED)
	{

		mle_init_cmd(&cmd,PARENT_RESPONSE);
		add_src_address_to_cmd(&cmd,MyNode.address);
		/* add the leader data tlv */
		add_leader_to_cmd(&cmd,thrd_generate_leader_data_tlv());

		// TODO add link-layer frame counter tlv
		// TODO add MLE Frame Counter tlv

		/* find the child */
		child= mle_find_child(*child_id);
		/* get the received challenge of the child */
		tlv_t resp ;
		resp.length=4;
		resp.value[0]= (uint8_t) ( child->challenge>> 24) & 0xFF ;
		resp.value[1]= (uint8_t) ( child->challenge>> 16) & 0xFF ;
		resp.value[2]= (uint8_t) ( child->challenge>> 8) & 0xFF ;
		resp.value[3]= (uint8_t)  child->challenge & 0xFF ;
		add_response_to_cmd(&cmd, &resp);

		/* store the challenge generated  */
		child->challenge= add_rand_challenge_to_cmd(&cmd);

		add_Link_margin_to_cmd(&cmd,child->rec_rssi);
		add_Cnnectivity_to_cmd(&cmd,MAX_CHILD,MyNode.childs_counter,count_neighbor_LQ(3),count_neighbor_LQ(2),count_neighbor_LQ(1),
				thrd_partition_get_leader_cost(),thrd_partition_get_id_seq_number());
		add_version_to_cmd(&cmd);

		/* to change by the child address */
		mle_send_msg( &cmd,&child->tmp);

		/* trigger the timer to remove the child after period of pending time (if the state wasn't changed to linked) */
		ctimer_set(&child->timer, 5 * bsp_getTRes() , check_child_state, (void *) &child->id );
	}
}

/**
 * \brief  assigning 16-bit short address to a child (based on router id and child id)
 *
 * \param  id	 child id
 *
 * \return 16-bit short address assigned to the child
 */

static uint16_t assign_address16(uint8_t id)
{
	return   (( thrd_iface_get_router_id() << 10) |  id | 0x0000  );
}

/**
 * \brief  State machine to handle the join process (attaching to a parent)
 *
 * \param  ptr	  always NULL
 */

static void mle_join_process(void *ptr)
{
	static 	jp_state_t 		jp_state;  // join process current state
	static mle_parent_t 	parent_condidate , current_parent; // parent
	tlv_connectivity_t*     connectivity;
	tlv_leader_t*           lead;
	tlv_t * tlv;

	uint8_t finish=0;
	static  uint8_t reponse_recived=0, req_sent_to_reed=0;; // flag to know if parent response received // flag to check whether the request to reed is sent or no

	if(MyNode.OpMode==NOT_LINKED)
	{
		while(!finish)
		{
			switch(jp_state)
			{
			case JP_SEND_MCAST_PR_TO_ROUTER:
				ctimer_stop(&parent_Timer);
				LOG_RAW("[+] ");
				LOG_RAW("JP Send mcast parent request to active router \n\r");

				/* Init the parent*/
				current_parent.is_Router=0;
				current_parent.LQ3=0;
				current_parent.LQ=0;

				/* Init the parent on the neighbor table*/
				parent=mle_find_nb_router(0);
				if(parent==NULL)
					parent=mle_add_nb_router(0, 0, 0, 0, 0);
				parent->state=PENDING;
				parent->LQ=0;
				parent->address16=0;
				MyNode.NB_router_couter=1;

				/* send parent request to the active router only and trigger the timer */
				jp_state=JP_PARENT_SELECT;
				send_mle_parent_request(1,0);
				LOG_RAW("[+] ");
				LOG_RAW("JP Waiting for incoming response from active router\n\r");
				ctimer_set(&c_mle_Timer, 2 * bsp_getTRes() , mle_join_process, NULL);
				req_sent_to_reed=0;
				finish=1;
				break;
			case JP_PARENT_SELECT:
				if(ctimer_expired(&c_mle_Timer)) // that means that this function was triggered by the ctimer
				{
					if(!reponse_recived)
					{
						if(!req_sent_to_reed)
						{
							jp_state=JP_SEND_MCAST_PR_TO_ROUTER_REED;
							reponse_recived=0;
						}
						else
							jp_state=JP_FAIL;
					}
					else
					{
						if(parent->LQ == 3 || req_sent_to_reed )
						{
							LOG_RAW("[+] ");
							LOG_RAW("JP Parent selection\n\r");
							LOG_RAW("link quality with parent : %i \n\r", parent->LQ);
							jp_state=JP_SEND_CHILD_REQ;
						}
						else
						{
							jp_state=JP_SEND_MCAST_PR_TO_ROUTER_REED;
							reponse_recived=0;
						}
					}
				}
				else
				{
					LOG_RAW("[+] ");
					LOG_RAW("JP received response from active Router\n\r");

					/* calculate the two-way link quality  */
					tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_LINK_MARGIN);
					LOG_RAW("my rssi : %i\n\r", tlv->value[0]);
					LOG_RAW("rssi of parent : %i\n\r", param.rec_rssi);
					parent_condidate.LQ=calculate_two_way_LQ(tlv->value[0], param.rec_rssi);
					LOG_RAW("two-way link quality : %i\n\r", parent_condidate.LQ);

					/* identify the type of the device router/reed */
					tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_SOURCE_ADDRESS);
					parent_condidate.is_Router=isActiveRouter(tlv->value[1] | (tlv->value[0] << 8));

					/* get number of neighbors with lq3 of the parent candidate */
					tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_CONNECTIVITY);
					tlv_connectivity_init(&connectivity,tlv->value );
					parent_condidate.LQ3=connectivity->LQ3;

					if( current_parent.LQ < parent_condidate.LQ ||
							((current_parent.LQ == parent_condidate.LQ) && (current_parent.is_Router < parent_condidate.is_Router)) ||
							( (current_parent.LQ == parent_condidate.LQ) && (current_parent.is_Router == parent_condidate.is_Router) && (current_parent.LQ3 < parent_condidate.LQ3)))
					{

						/* update the current parent */
						current_parent.LQ=parent_condidate.LQ;
						current_parent.LQ3=parent_condidate.LQ3;
						current_parent.is_Router=parent_condidate.is_Router;

						/* save the two-way link quality */
						parent->LQ=current_parent.LQ;

						/* save the two-way link quality */
						parent->rec_rssi=param.rec_rssi;

						/* save the challenge TLV from the parent candidate */
						tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_CHALLENGE);
						parent->challenge= (tlv->value[3]) | (tlv->value[2] << 8) | (tlv->value[1] << 16)| (tlv->value[0] << 24);

						/* save the address */
						uip_ip6addr_copy(&parent->tmp , &param.source_addr);
						tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_SOURCE_ADDRESS);
						parent->address16=tlv->value[1] | (tlv->value[0] << 8);
					}

					reponse_recived=1;
					finish=1;
				}
				break;
			case JP_SEND_MCAST_PR_TO_ROUTER_REED:
				/* send parent request to the active router and reed and trigger the timer */
				LOG_RAW("[+] ");
				LOG_RAW("JP Send mcast parent request to active Router and REED\n\r");
				jp_state=JP_PARENT_SELECT;
				send_mle_parent_request(1,1);
				LOG_RAW("[+] ");
				LOG_RAW("JP Waiting for incoming response from active Router and REED\n\r");
				ctimer_set(&c_mle_Timer, 3 * bsp_getTRes() , mle_join_process, NULL);
				req_sent_to_reed=1;
				finish=1;
				break;
			case JP_SEND_CHILD_REQ:
				/* send child id request to the selected parent */
				LOG_RAW("[+] ");
				LOG_RAW("JP send Child ID Request \n\r");
				send_mle_childID_request(&parent->tmp);
				jp_state=JP_SAVE_PARENT;
				finish=1;
				break ;
			case JP_SAVE_PARENT:
				LOG_RAW("[+] ");
				LOG_RAW("JP Parent Stored \n\r");
				/* change the state of the parent to linked */
				parent->state=LINKED;

				/* update the parent address (may be is was a REED and then become router) */
				tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_SOURCE_ADDRESS);
				parent->address16=tlv->value[1] | (tlv->value[0] << 8);
				uip_ip6addr_copy(&parent->tmp , &param.source_addr);

				/* process leader data tlv */
				/* TODO verify if this should be done here */
				tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_CONNECTIVITY);
				tlv_connectivity_init(&connectivity,tlv->value);
				tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_LEADER_DATA);
				tlv_leader_init(&lead,tlv->value);
				thrd_partition_process_leader_tlv(lead);	// TODO We don't have the ID sequence number.
				/* reset the state in case we reattach to a new parent after losing connectivity */
				jp_state=JP_SEND_MCAST_PR_TO_ROUTER;

				/* start operating as child */
				tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_ADDRESS16);
				mle_set_child_mode(tlv->value[1] | (tlv->value[0] << 8));

				finish=1;
				break ;
			case JP_FAIL:
				LOG_RAW("Joining process failed.\n\r");
				LOG_RAW("Starting new partition.\n\r");

				thrd_partition_start();
				mle_set_parent_mode();
				PRESET();
				/* reset the state in case we reattach to a new parent after losing connectivity */
				jp_state=JP_SEND_MCAST_PR_TO_ROUTER;
				finish=1;
				break;
			}
		}
	}
}


/************************ MLE synchronisation process ****************************/

/**
 * \brief  Send MLE child update message
 *
 * \param  dest_addr	destination address (usually the parent address)
 * \return
 *       -  1 success
 *       -  0 error
 */

static uint8_t send_mle_child_update(uip_ipaddr_t *dest_addr)
{
	mle_init_cmd(&cmd,CHILD_UPDATE);
	add_mode_RSDN_to_cmd(&cmd, MyNode.rx_on_when_idle, 0, IS_FFD, 0);
	add_src_address_to_cmd(&cmd,MyNode.address);
	add_leader_to_cmd(&cmd,thrd_generate_leader_data_tlv());
	if(mle_send_msg(&cmd, dest_addr))
		return 1;
	else
		return 0;
}


/**
 * \brief  Send MLE child update response message
 *
 * \param  dest_addr	destination address
 * \return
 *       -  1 success
 *       -  0 error
 */

static uint8_t send_mle_child_update_response(uip_ipaddr_t *dest_addr)
{
	mle_init_cmd(&cmd,CHILD_UPDATE_RESPONSE);
	add_mode_RSDN_to_cmd(&cmd, MyNode.rx_on_when_idle, 0, IS_FFD, 0);
	add_src_address_to_cmd(&cmd,MyNode.address);
	add_leader_to_cmd(&cmd,thrd_generate_leader_data_tlv());
	if(mle_send_msg(&cmd, dest_addr))
		return 1;
	else
		return 0;
}


/**
 * \brief  Send multicast MLE link request message
 *
 * \return
 *       -  1 success
 *       -  0 error
 */

static uint8_t send_mle_link_request(void)
{
	mle_init_cmd(&cmd,LINK_REQUEST);
	add_src_address_to_cmd(&cmd,MyNode.address);
	add_leader_to_cmd(&cmd,thrd_generate_leader_data_tlv());
	MyNode.challenge=add_rand_challenge_to_cmd(&cmd);
	add_version_to_cmd(&cmd);
	// TLV request : link Margin
	uip_create_linklocal_allrouters_mcast(&s_destAddr);
	if(mle_send_msg(&cmd, &s_destAddr))
		return 1;
	else
		return 0;
}

/**
 * \brief  replying for an MLE link request message
 *
 * \param  ptr	neighbor id
 */

static void reply_for_mle_link_request(void *ptr)
{
	uint8_t* router_id;
	router_id= (uint8_t*) ptr;
	tlv_t resp ;

	if(MyNode.OpMode== PARENT)
	{
		mle_init_cmd(&cmd,LINK_ACCEPT);
		add_src_address_to_cmd(&cmd,MyNode.address);
		add_leader_to_cmd(&cmd,thrd_generate_leader_data_tlv());
		//add_response_to_cmd(&cmd, mle_find_tlv_in_cmd(param.rec_cmd,TLV_CHALLENGE));
		// link-layer frame counter tlv
		add_version_to_cmd(&cmd);

		add_Link_margin_to_cmd(&cmd,param.rec_rssi);

		/*  TODO If the recipient of a Link Request message has a valid frame
		 * counter for the sender, it sends a Link Accept in reply;
		 *  if not, it sends a Link Accept and Request.*/

		/* find the nb router */
		nb = thrd_rdb_link_lookup(*router_id);

		if(nb->L_link_margin == 0 && nb->L_outgoing_quality == 0 ) // that means the device is not linked yet
		{
			cmd.type=LINK_ACCEPT_AND_REQUEST;
			/* MLE Frame Counter tlv */
			// TODO add frame counter
			/* add and store the challenge generated  */
			MyNode.challenge= add_rand_challenge_to_cmd(&cmd);

			MyNode.syn_state=SYN_PROCESS_LINK;
			ctimer_set(&c_mle_Timer, 2 * bsp_getTRes() , mle_synchro_process, NULL);
		}

		/* get the received challenge of the child */
		resp.length=4;
		resp.value[0]= (uint8_t) ( nb->challenge>> 24) & 0xFF ;
		resp.value[1]= (uint8_t) ( nb->challenge>> 16) & 0xFF ;
		resp.value[2]= (uint8_t) ( nb->challenge>> 8) & 0xFF ;
		resp.value[3]= (uint8_t)  nb->challenge & 0xFF ;
		add_response_to_cmd(&cmd, &resp);

		/* send the reply */
		uip_ip6addr(&s_destAddr, 0xfe80, 0, 0, 0, 0, 0x00ff, 0xfe00, (*router_id) << 10 );
		mle_send_msg( &cmd,&s_destAddr);

	}
}


/**
 * \brief  MLE synchronisation process: usually triggered after start operating as parent
 * to synchronise with other neighbor routers
 *
 * \param  ptr	always NUll
 */

static void mle_synchro_process(void *ptr)
{
	uint8_t finish=0;
	uint8_t router_id;
	tlv_t*  tlv;

	if(MyNode.OpMode==PARENT)
	{
		while(!finish)
		{
			switch(MyNode.syn_state)
			{
			case SYN_SEND_LINK_REQUEST:
				LOG_RAW("[+] ");
				LOG_RAW("SNY process: Send Link Request to neighbor router.\n\r");
				/* send MLE Link Request */
				send_mle_link_request();
				/* change the state and trigger the timer */
				MyNode.syn_state=SYN_PROCESS_LINK;
				ctimer_set(&c_mle_Timer, 2 * bsp_getTRes() , mle_synchro_process, NULL);
				finish=1;
				break;
			case SYN_PROCESS_LINK:
				if(ctimer_expired(&c_mle_Timer)) // that means that this function was triggered by the ctimer
				{
					/* initialise the state */
					MyNode.syn_state=SYN_SEND_LINK_REQUEST;
					LOG_RAW("[+] ");
					LOG_RAW("SNY process:  Synchronization process finished.\n\r");

				}
				else
				{
					LOG_RAW("[+] ");
					LOG_RAW("SNY process: processing link.\n\r");

					/* get the router id from the LL ipv6 address */
					router_id= (uint8_t) (param.source_addr.u8[14]>>2);
					/* get the link margin tlv */
					tlv=mle_find_tlv_in_cmd(param.rec_cmd,TLV_LINK_MARGIN);
					/* update neighbor values */
					nb=thrd_rdb_link_update(router_id,  tlv->value[0] ,  param.rec_rssi ,  0);

					if(nb)
					{
						PRINTFG("[+] ");
						PRINTF("SNY process: Neighbor added.\n\r");

						/* if the reply was link accept and request then we have to reply for it */
						if(param.rec_cmd->type==LINK_ACCEPT_AND_REQUEST)
						{
							reply_for_mle_link_request(&nb->L_router_id);
						}
					}
					else
					{
						LOG_RAW("[+] ");
						LOG_RAW("SNY process: Failed to allocate a new neighbor route.\n\r");
					}
				}
				finish=1;
				break;
			}
		}
	}
}


/**
 * \brief  Handling keep-alive messages: used by the child to keep
 * linked with its  parent node
 *
 * \param  ptr	always NULL
 */
static void mle_keep_alive(void *ptr)
{
	static uint8_t nbr_retry=0; // the max retry is 3
	uint8_t finish=0;
	static ka_state_t state=KA_SEND_KEEP_ALIVE ;  // state of the  keep alive
	if(MyNode.OpMode==CHILD)
	{
		while (!finish)
		{
			switch (state)
			{
			case KA_SEND_KEEP_ALIVE : //  send keep alive message to the parent
				if (MyNode.rx_on_when_idle)
				{
					send_mle_child_update(&parent->tmp);
					nbr_retry++;
				}
				else
				{
					// TODO : The keep-alive message for an rx-off-when-idle Children is an 802.15.4 Data Request command

				}

				state=KA_WAIT_RESPONSE;
				/* set the timer to wait for a response within 2s  */
				ctimer_set(&parent_Timer, 2 * bsp_getTRes() , mle_keep_alive, NULL);
				finish++;

				break;
			case KA_WAIT_RESPONSE : // waiting for response
				if(ctimer_expired(&parent_Timer)) // that means that this function was triggered by the timer ==> no response received from the parent
				{
					if(nbr_retry >= 3 )	/* retry */
					{
						nbr_retry=0;
						/* set my node as not linked and redo the join process */
						MyNode.OpMode=NOT_LINKED;
						ctimer_set(&parent_Timer, 2 * bsp_getTRes() , mle_join_process , NULL );
						finish++;
					}
				}
				else // a response to keep-alive message was received
				{
					/* reset the counter of retry */
					nbr_retry=0;
					/* trigger the timer to count again*/
					ctimer_set(&parent_Timer-2 , MyNode.timeOut * bsp_getTRes() , mle_keep_alive , NULL );
					finish++;
				}
				/* set the state to send keep alive message next time*/
				state=KA_SEND_KEEP_ALIVE;
				break;
			}
		}
	}
}


/************************ process incoming MLE messages function ****************************/

/**
 * \brief  processing all incoming MLE messages
 *
 * \param c    A pointer to the struct udp_socket that received the data
 * \param ptr  An opaque pointer that was specified when the UDP socket was registered with udp_socket_register()
 * \param source_addr The IP address from which the datagram was sent
 * \param source_port The UDP port number, in host byte order, from which the datagram was sent
 * \param dest_addr The IP address that this datagram was sent to
 * \param dest_port The UDP port number, in host byte order, that the datagram was sent to
 * \param data A pointer to the data contents of the UDP datagram
 * \param datalen The length of the data being pointed to by the data pointer
 *
 */

static void  _mle_process_incoming_msg(struct udp_socket *c, void *ptr, const uip_ipaddr_t *source_addr, uint16_t source_port,
		const uip_ipaddr_t *dest_addr, uint16_t dest_port, const uint8_t *data, uint16_t datalen)
{
	mle_cmd_t * 	cmd;
	tlv_t* 			tlv;
	tlv_route64_t* 	route64_tlv;
	tlv_leader_t* 	leader_tlv;
	uint16_t MaxRand;
	uint8_t id=0;

	mle_create_cmd_from_buff(&cmd , (uint8_t * )data , datalen);

	LOG_RAW("<== MLE ");
	mle_print_type_cmd(*cmd);
	LOG_RAW(" received from : ");
	LOG_IP6ADDR(source_addr);
	LOG_RAW("\n\r");
	mle_print_cmd(*cmd);

	switch (cmd->type)
	{
	case LINK_REQUEST:
		/* check if neighbors table is not full */
		if(MyNode.OpMode== PARENT)
		{

			/* check the nb already exist */
			nb = thrd_rdb_link_lookup((source_addr->u8[14] >> 2 ));

			if(!nb)// the neighbor is not existing
			{
				/* add the neighbor with infinite value */
				nb=thrd_rdb_link_update( (source_addr->u8[14] >> 2 ), 0, 0, 0);
				/* store the received challenge*/
				tlv=mle_find_tlv_in_cmd(cmd,TLV_CHALLENGE);

				if(nb)
				{
					nb->challenge= (tlv->value[3]) | (tlv->value[2] << 8) | (tlv->value[1] << 16)| (tlv->value[0] << 24);
					/* store the address of the nb router */
					LOG_RAW(" neighbor added \n");
				}
				else
				{
					LOG_RAW(" failed to add neighbor \n");

				}
			}


			/* prepare param */
			param.rec_cmd=cmd;
			param.rec_rssi=get_rssi();
			uip_ip6addr_copy(&param.source_addr,source_addr);

			/* trigger the timer to reply after the random period */
			ctimer_set(&nb->L_age, bsp_getrand(0, 500) *  (bsp_getTRes() / 1000 ),
								 reply_for_mle_link_request, (void *) &nb->L_router_id );
		}
		break;
	case LINK_ACCEPT:
		if (MyNode.OpMode==PARENT)
		{
			/* check the response TLV before processing the parent response */
			tlv=mle_find_tlv_in_cmd(cmd,TLV_RESPONSE);
			if(comp_resp_chall(MyNode.challenge,tlv->value))
			{
				param.rec_cmd=cmd;
				param.rec_rssi=get_rssi();
				uip_ip6addr_copy(&param.source_addr,source_addr);

				mle_synchro_process(NULL);

			}
		}
		break;
	case LINK_ACCEPT_AND_REQUEST:
		if (MyNode.OpMode==PARENT)
		{
			/* check the response TLV before processing the parent response */
			tlv=mle_find_tlv_in_cmd(cmd,TLV_RESPONSE);
			if(comp_resp_chall(MyNode.challenge,tlv->value))
			{
				param.rec_cmd=cmd;
				param.rec_rssi=get_rssi();
				uip_ip6addr_copy(&param.source_addr,source_addr);

				mle_synchro_process(NULL);

			}
		}
		break;
	case LINK_REJECT:
		break;
	case ADVERTISEMENT:

		if(MyNode.OpMode == PARENT)
		{
			tlv=mle_find_tlv_in_cmd(cmd,TLV_ROUTE64);
			tlv_route64_init(&route64_tlv,tlv->value);
			tlv=mle_find_tlv_in_cmd(cmd,TLV_LEADER_DATA);
			tlv_leader_init(&leader_tlv,tlv->value);
			tlv=mle_find_tlv_in_cmd(cmd,TLV_SOURCE_ADDRESS);
			thrd_process_adv( tlv->value[1] | (tlv->value[0] << 8), route64_tlv,leader_tlv);
			LOG_RAW("MLE advertisement processed.\n\r");
		}
		break;

	case UPDATE:
		break;
	case UPDATE_REQUEST:  // Not used in Thread Network
		break;
	case DATA_REQUEST:  // Not used in Thread Network
		break;
	case DATA_RESPONSE:
		break;
	case PARENT_REQUEST:
		/* check if child table is not full */
		if(MyNode.childs_counter<MAX_CHILD && MyNode.OpMode!= NOT_LINKED)
		{
			/* check: A Router MUST NOT send an MLE  Parent Response if:
					It is disconnected from its Partition (that is, it has not received
					an updated ID sequence number within LEADER_TIMEOUT seconds
				OR
		 			Its current routing path cost to the Leader is infinite.
			 */

			/* check the scan mask TLV to know if i should reply or not */
			tlv=mle_find_tlv_in_cmd(cmd,TLV_SCAN_MASK);
			if((MyNode.OpMode==PARENT && routerMask(tlv->value[0]) ) || (MyNode.OpMode==CHILD && ReedMask(tlv->value[0]) ) )
			{
				/* handle the random time to wait before reply */
				if(ReedMask(tlv->value[0]))
					MaxRand=1000;// max response after 1000 ms
				else
					MaxRand=500;// max response after 500 ms

				/* find the mode tlv of the sender in order to store it */
				tlv=mle_find_tlv_in_cmd(cmd,TLV_MODE);

				/* allocate temporarily a child */
				id=1; // don't use the id zero for a child : problem with the short address
				do
				{
					child=mle_add_child( id , 0 /*adress16*/ ,  0 /*frame counter*/, tlv->value[0] /*modeTLV*/,  0 /*link quality */);
					id++;
				}
				while (child==NULL && id<MAX_CHILD);

				if (child)
				{
					/* increment the child counter */
					MyNode.childs_counter++;
					/* set the state of the child to pending*/
					child->state=PENDING;
					/* store the address of the child */
					uip_ip6addr_copy(&child->tmp ,source_addr);
					/* store the received challenge*/
					tlv=mle_find_tlv_in_cmd(cmd,TLV_CHALLENGE);
					child->challenge= (tlv->value[3]) | (tlv->value[2] << 8) | (tlv->value[1] << 16)| (tlv->value[0] << 24);
					/* store the received rssi */
					child->rec_rssi=get_rssi();

					LOG_RAW("ID allocated for the child = %i\n\r", child->id);
				}
				else
				{
					LOG_RAW(ANSI_COLOR_RED"Failed to allocate child id.\n\r");PRESET();
					break;
				}

				/* trigger the timer to reply after the random period */
				ctimer_set(&child->timer, bsp_getrand(0, MaxRand) *  (bsp_getTRes() / 1000 ) , reply_for_mle_parent_request, (void *) &child->id );

			}
		}
		break;
	case PARENT_RESPONSE:

		if (MyNode.OpMode==NOT_LINKED)
		{
			/* check the response TLV before processing the parent response */
			tlv=mle_find_tlv_in_cmd(cmd,TLV_RESPONSE);
			if(comp_resp_chall(MyNode.challenge,tlv->value))
			{
				param.rec_cmd=cmd;
				param.rec_rssi=get_rssi();
				uip_ip6addr_copy(&param.source_addr,source_addr);


				mle_join_process(NULL);

			}
		}
		break;
	case CHILD_ID_REQUEST:
		if (MyNode.OpMode!=NOT_LINKED)
		{
			/*
			 * If the Mode TLV indicates that the sender is an rx-off-when-idle device, it
			 * MUST begin sending IEEE 802.15.4 data requests after sending the Child ID request
			 */

			/* find child using source address */
			child= mle_find_child_byAdd( (uip_ipaddr_t *) source_addr);
			if(!child)
			{
				LOG_RAW("Child not found ...");PRESET();
				return;
			}

			/* check the response TLV before processing the parent response */
			tlv=mle_find_tlv_in_cmd(cmd,TLV_RESPONSE);
			if(comp_resp_chall(child->challenge ,tlv->value))
			{
				/* store child's time out that we get from timeout TLV */
				tlv=mle_find_tlv_in_cmd(cmd,TLV_TIME_OUT);
				child->time_out=(tlv->value[3]) | (tlv->value[2] << 8) | (tlv->value[1] << 16)| (tlv->value[0] << 24);

				param.rec_cmd=cmd;
				param.rec_rssi=get_rssi();
				uip_ip6addr_copy(&param.source_addr,source_addr);
				reply_for_mle_childID_request(NULL);
			}
		}
		break;
	case CHILD_ID_RESPONSE:
		if (MyNode.OpMode==NOT_LINKED)
		{
			/* update the param variable */
			param.rec_cmd=cmd;
			param.rec_rssi=get_rssi();
			uip_ip6addr_copy(&param.source_addr,source_addr);
			/* call the join process fct */
			mle_join_process(NULL);
		}
		break;
	case CHILD_UPDATE:
		if (MyNode.OpMode!=NOT_LINKED)
		{
			tlv=mle_find_tlv_in_cmd(cmd,TLV_SOURCE_ADDRESS);
			/* find child using source address */
			child = mle_find_child_by_16Add( (tlv->value[1]) | (tlv->value[0] << 8) );
			if(child)
			{
				// TODO Update value if there is changes
				/* send child id update response */
				send_mle_child_update_response(&child->tmp);
				/* restart the time out timer since we received a child update command */
				ctimer_set(&child->timer, child->time_out * bsp_getTRes() , time_out_remove_child, (void *) &child );
			}
			else
				LOG_RAW("Child not found.\n\r");

		}
		break;
	case CHILD_UPDATE_RESPONSE:
		if (MyNode.OpMode!=NOT_LINKED)
		{
		mle_keep_alive(NULL);
		}
		break;
	default :
		LOG_RAW(ANSI_COLOR_RED"Error: MLE received command not recognized \n\r"ANSI_COLOR_RESET);
		break;
	}

}

/************************ init MLE protocol  ****************************/

/**
 * \brief  Initialise  UDP connection
 *
 * \return
 *       -  1 success
 *       -  0 error
 */

static uint8_t mle_init_udp(void)
{
	udp_socket_close(&MyNode.udp_socket);
	udp_socket_register(&MyNode.udp_socket, NULL, _mle_process_incoming_msg);
	udp_socket_bind(&MyNode.udp_socket, MLE_UDP_LPORT);
	udp_socket_connect(&MyNode.udp_socket,NULL, MLE_UDP_RPORT);

	if (&MyNode.udp_socket.udp_conn == NULL)
	{
		LOG_RAW(ANSI_COLOR_RED "No UDP connection available, error!\n\r"ANSI_COLOR_RESET);
		return 0;
	}

	LOG_RAW("MLE UDP initialized : \n lport --> %u \n rport --> %u \n", UIP_HTONS(MyNode.udp_socket.udp_conn->lport),
			UIP_HTONS(MyNode.udp_socket.udp_conn->rport));

	return 1 ;
}

/************************  MLE send message function  ****************************/
/**
 * \brief  Initialise  UDP connection
 *
 * \param  cmd            pointer to the MLE command to send
 * \param  dest_addr      destination address
 *
 * \return
 *       -  1 success
 *       -  0 error
 */
static uint8_t mle_send_msg(mle_cmd_t* cmd,  uip_ipaddr_t *dest_addr )
{
	/* check UDP connection */
	if (&MyNode.udp_socket.udp_conn == NULL)
	{
		LOG_RAW("reinitialize UPD");
		if(!mle_init_udp())
			return 0;
	}

	/* send the UDP packet */
	if(!udp_socket_sendto(&MyNode.udp_socket, (uint8_t*) cmd , cmd->used_data+1 , dest_addr ,MLE_UDP_RPORT))
	{
		LOG_RAW(ANSI_COLOR_RED "Failed to send MLE msg\n"ANSI_COLOR_RESET);
		return 0;
	}

	LOG_RAW("==> MLE ");
	mle_print_type_cmd(*cmd);
	LOG_RAW( " sent to : "  );
	LOG_IP6ADDR(dest_addr);
	LOG_RAW("\n\r");

	return 1 ;


} /* _mle_sendMsg */

/*==============================================================================
                                 API FUNCTIONS
 =============================================================================*/


uint8_t mle_init(void)
{

	/*  Initialise  MLE UDP connexion */
	if(!mle_init_udp()) return 0 ;

	/* Initialise  nb table */
	mle_nb_device_init();

	/* Initialise  MLE node structure */
	MyNode.OpMode=NOT_LINKED;
	MyNode.timeOut=TIME_OUT;
	MyNode.rx_on_when_idle=IS_RX_ON_WHEN_IDLE;
	MyNode.childs_counter=0;

	/* Trigger the timer for the join process */
	ctimer_set(&c_mle_Timer, 1 * bsp_getTRes() , mle_join_process, (void *) NULL );

	LOG_RAW("MLE protocol initialized.\n\r");;
	return 1;

};


uint8_t send_mle_advertisement(tlv_route64_t* route, uint8_t len, tlv_leader_t* lead)
{
	if(MyNode.OpMode == PARENT)
	{
		mle_init_cmd(&cmd,ADVERTISEMENT);
		add_src_address_to_cmd(&cmd, MyNode.address);
		add_route64_to_cmd(&cmd,route,len);
		add_leader_to_cmd(&cmd,lead);
		uip_create_linklocal_allnodes_mcast(&s_destAddr);
		mle_send_msg(&cmd, &s_destAddr);
	}
	return 1 ;

}


void reply_for_mle_childID_request(void *ptr)
{
	uint8_t* routerId;
	routerId=(uint8_t*) ptr;
	size_t len =0;
	tlv_route64_t* route64;
	route64=thrd_generate_route64(&len);

	if(ptr!=NULL)
	{
		if(*routerId<63)
		{
			thrd_iface_set_router_id(*routerId);
			mle_set_parent_mode();
		}
		else
			// TODO send error to child
			return;

	}

	if(MyNode.OpMode == CHILD)
	{
		// TODO verify if i already send a request for router id (may be inside the function using static variable )
		/* send request to become a router and then reply  */
		LOG_RAW("Sending request to become a Router\n\r");
		thrd_request_router_id(NULL);

	}
	else if (MyNode.OpMode == PARENT)
	{
		/* find the child and assign a 16-bit address for it */
		child= mle_find_child_byAdd(&param.source_addr);
		child->address16=assign_address16(child->id);

		mle_init_cmd(&cmd,CHILD_ID_RESPONSE);
		add_src_address_to_cmd(&cmd, MyNode.address);
		add_leader_to_cmd(&cmd,thrd_generate_leader_data_tlv());
		add_address16_to_cmd(&cmd , child->address16 );
		add_route64_to_cmd(&cmd,route64,len);
		mle_send_msg( &cmd,&param.source_addr);
		child->state=LINKED;
		/*  update the temp address with the one based on the 16-bit address */
		uip_ip6addr(&child->tmp, 0xfe80, 0, 0, 0, 0, 0x00ff, 0xfe00, child->address16);

	}

}

void mle_set_parent_mode()
{
	/* stop the keep-alive process if we are operating as child */
	if(MyNode.OpMode==CHILD)
	{
		ctimer_stop(&parent_Timer);
	}
	/* set the operating mode as router (parent) */
	MyNode.OpMode=PARENT;
	LOG_RAW("MLE : Node operating as Parent.\n\r");

	/* clear the neighbors router table  */
	mle_rm_all_nb_router();


	/* set the rloc address */
	thrd_iface_set_rloc(thrd_iface_get_router_id()<< 10);

	/* set the Mac short address */
	linkaddr_t add;
	MyNode.address=thrd_iface_get_router_id()<<10;

	add.u8[7]=(uint8_t)MyNode.address ;
	add.u8[6]=MyNode.address>>8 ;

	linkaddr_set_node_shortAddr(&add);

	/* trigger the timer for the synchronisation process */
	ctimer_set(&c_mle_Timer, 2 * bsp_getTRes() , mle_synchro_process , NULL );

	/* start the trickle algorithm */
	thrd_trickle_init();

}

void mle_set_child_mode(uint16_t rloc16)
{

	/* update the 16-bit short address  */
	MyNode.address=rloc16;
	/* set the operating mode as child */
	MyNode.OpMode=CHILD;
	LOG_RAW("MLE : Node operating as Child\n\r");

	/* set the rloc address */
	thrd_iface_set_rloc(rloc16);

	/* set the Mac short address */
	linkaddr_t add;
	add.u8[7]=(uint8_t)rloc16 ;
	add.u8[6]=rloc16>>8 ;
	linkaddr_set_node_shortAddr(&add);

	/* clear the child table  */
	mle_rm_all_child();

	/* stop the trickle algo*/
	thrd_trickle_stop();

	/* trigger the timer to start sending keep alive messages */
	ctimer_set(&parent_Timer, MyNode.timeOut * bsp_getTRes() , mle_keep_alive , NULL );


}
