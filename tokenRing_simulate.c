/*
 * The program simulates a Token Ring LAN by forking off a process
 * for each LAN node, that communicate via shared memory, instead
 * of network cables. To keep the implementation simple, it jiggles
 * out bytes instead of bits.
 *
 * It keeps a count of packets sent and received for each node.
 */
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "tokenRing.h"

/*
 * This function is the body of a child process emulating a node.
 */
void
token_node(control, num)
	struct TokenRingData *control;
	int num;
{
	int rcv_state = TOKEN_FLAG, not_done = 1, sending = 0, len, sent, to;
	//token flag means something to send?
	unsigned char byte;

	/*
	 * If this is node #0, start the ball rolling by creating the
	 * token.
	 */
	if (num == 0) {
		send_byte(control, num, '/');
	}

	/*
	 * Loop around processing data, until done.
	 */
	//critial sem
	while (not_done) {


		byte = rcv_byte(control, num);

		//order for sending is token_flag->to->from->len->data

		/*
		 * Handle the byte, based upon current state.
		 */
		switch (rcv_state) {
		case TOKEN_FLAG:

			if (byte == '/') {
				//free to send data
				
				WAIT_SEM(control, CRIT)
				if (control->shared_ptr->node[num].to_send.token_flag == '0') {
					//we can send packet!
					sending = 1;
					
				} else {
					//free to send, but nothing to send from this node
					sending = 0;
				}
				SIGNAL_SEM(control, CRIT);


				if (sending) {
					//start process of sending our packet

					WAIT_SEM(control, CRIT);
					control->snd_state = TOKEN_FLAG;
					SIGNAL_SEM(control, CRIT);
					send_pkt(control, num);
					rcv_state = TO; //not sure if should be changing rcv state... but whatever for now
				} else {
					//nothing to send from here
					WAIT_SEM(control, CRIT);
					//are we done?
					if (control->shared_ptr->node[num].terminate) {
						not_done = 0;
					}

					SIGNAL_SEM(control, CRIT);

					send_byte(control, num, byte);
					rcv_state = TOKEN_FLAG;

				}

			} else if (byte == '+') {
				//starting a new string
				send_byte(control, num, byte); //pass it along
				rcv_state = TO;
			} else {
				//recieving gunk, pass period
				send_byte(control, num, '.'); //
			}


			break;

		case TO:
			if (sending) {
				send_pkt(control, num);
			} else {
				to = (int) byte;
				send_byte(control, num, byte);
			}
			rcv_state = FROM;
			break;

		case FROM:
			if (sending) {
				send_pkt(control, num);
			} else {
				send_byte(control, num, byte);
			}
			rcv_state = LEN;
			break;

		case LEN:
			if (sending) {
				send_pkt(control, num);
				WAIT_SEM(control, CRIT);
				len = control->shared_ptr->node[num].to_send.length;
				SIGNAL_SEM(control, CRIT);
			} else {
				send_byte(control, num, byte);
				len = (int) byte;
			}

			if (len > 0) {
				rcv_state = DATA;
			} else {
				rcv_state = TOKEN_FLAG;
			}

			break;

		case DATA:
			
			if (len > 0) {
				if (sending) {
					send_pkt(control, num);
				} else {
					send_byte(control, num, byte);
				}
				len--;
			} else {
				//should have sent/recieved all data
				if (sending) {
					WAIT_SEM(control, CRIT);
					control->shared_ptr->node[num].sent++;
					SIGNAL_SEM(control, CRIT);
					send_byte(control, num, '/');
				} else {
					if (to == num) {
						WAIT_SEM(control, CRIT);
						control->shared_ptr->node[num].received++;
						SIGNAL_SEM(control, CRIT);
					}
					send_byte(control, num, byte);
				}

				sending = 0;
				rcv_state = TOKEN_FLAG;
			}

			break;
		};
	}
	//critial sem
}

/*
 * This function sends a data packet followed by the token, one byte each
 * time it is called.
 */
void
send_pkt(control, num)
	struct TokenRingData *control;
	int num;
{
	static int sndpos, sndlen;

	switch (control->snd_state) {
	case TOKEN_FLAG:
		...
		break;

	case TO:
		...
		break;

	case FROM:
		...
		break;

	case LEN:
		...
		break;

	case DATA:
		...
		break;

	case DONE:
		break;
	};
}

/*
 * Send a byte to the next node on the ring.
 */
void
send_byte(control, num, byte)
	struct TokenRingData *control;
	int num;
	unsigned byte;
{
	...
}

/*
 * Receive a byte for this node.
 */
unsigned char
rcv_byte(control, num)
	struct TokenRingData *control;
	int num;
{
	unsigned char byte;

	...
}

