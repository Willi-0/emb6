/* -*- C -*- */
/*
 * Copyright (c) 2005, Swedish Institute of Computer Science
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */
/**
 * \file
 *         A brief description of what this file is
 */

#ifndef SLIP_H_
#define SLIP_H_



#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM          			4

#undef UIP_CONF_BUFFER_SIZE
#define UIP_CONF_BUFFER_SIZE    			140

#undef UIP_CONF_ROUTER
#define UIP_CONF_ROUTER                 	FALSE

#undef UIP_CONF_IPV6_RPL
#define UIP_CONF_IPV6_RPL               	FALSE

#define CMD_CONF_OUTPUT 					slip_radio_cmd_output

#define CMD_CONF_HANDLERS 					slip_radio_cmd_handler,cmd_handler_rf230


#undef NULLRDC_CONF_802154_AUTOACK
#define NULLRDC_CONF_802154_AUTOACK     	FALSE

#undef UART1_CONF_RX_WITH_DMA
#define UART1_CONF_RX_WITH_DMA           	TRUE

#undef UART1_CONF_TX_WITH_INTERRUPT
#define UART1_CONF_TX_WITH_INTERRUPT     	TRUE

#define UART1_CONF_TXBUFSIZE             	512

#define UART1_CONF_RXBUFSIZE             	512

#define SLIP_CONF_TCPIP_INPUT()

/**
 * Send an IP packet from the uIP buffer with SLIP.
 */
uint8_t slip_send(void);

/**
 * Input a SLIP byte.
 *
 * This function is called by the RS232/SIO device driver to pass
 * incoming bytes to the SLIP driver. The function can be called from
 * an interrupt context.
 *
 * For systems using low-power CPU modes, the return value of the
 * function can be used to determine if the CPU should be woken up or
 * not. If the function returns non-zero, the CPU should be powered
 * up. If the function returns zero, the CPU can continue to be
 * powered down.
 *
 * \param c The data that is to be passed to the SLIP driver
 *
 * \return Non-zero if the CPU should be powered up, zero otherwise.
 */
void slip_input_byte(void * c);

uint8_t slip_write(const void *ptr, int len);

/* Did we receive any bytes lately? */
extern uint8_t slip_active;

/* Statistics. */
extern uint16_t slip_rubbish, slip_twopackets, slip_overflow, slip_ip_drop;

/**
 * Set a function to be called when there is activity on the SLIP
 * interface; used for detecting if a node is a gateway node.
 */
void slip_set_input_callback(void (*callback)(void));
void slip_init(void);

#endif /* SLIP_H_ */
