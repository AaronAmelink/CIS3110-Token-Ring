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
	int rcv_state = TOKEN_FLAG, not_done = 1, sending = 0, len, sent = 0, to, sendLen;
	//token flag to my understanding is the "waiting to recieve stuff" flag
	//sent shows if I sent the first message I'm reading since sending. i.e. if I send a message, the first one I read next should be my own.
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

	//we should get len



	//critial sem
	while (not_done) {

		//order for sending is token_flag->to->from->len->data


		if (sending) {
#ifdef DEBUG
		fprintf(stderr, "calling send packet %d\n", sendLen);
#endif
			send_pkt(control, num);
			if (--sendLen == 0 ){
				sending = 0;
				sent = 1;
			}
		} 

		byte = rcv_byte(control, num);

		if (sending) {
			continue;
		}


		/*
		 * Handle the byte, based upon current state.
		 */
		switch (rcv_state) {
		case TOKEN_FLAG:



			if (byte == '/') {
				//free to send data
				
				WAIT_SEM(control, CRIT);
				if (control->shared_ptr->node[num].to_send.token_flag == '0') {
					//we can send packet!
#ifdef DEBUG
		fprintf(stderr, "starting packet send from %d\n", num);
#endif
					sending = 1;
					control->snd_state = TOKEN_FLAG;
					sendLen = control->shared_ptr->node[num].to_send.length + 4; //to from len
					
				} else {
					//free to send, but nothing to send from this node
					sending = 0;
				}
				SIGNAL_SEM(control, CRIT);


				if (!sending) {
					//nothing to send from here
					WAIT_SEM(control, CRIT);
					//are we done?
					if (control->shared_ptr->node[num].terminate) {
						not_done = 0;
#ifdef DEBUG
		fprintf(stderr, "terminating %d\n", num);
#endif
					}

					SIGNAL_SEM(control, CRIT);

					send_byte(control, num, byte);
					rcv_state = TOKEN_FLAG;

				}

			} else if (byte == '+') {
				//starting a new string
				if (!sent) {
					send_byte(control, num, byte); //pass it along
				}
				rcv_state = TO;
#ifdef DEBUG
        fprintf(stderr, "rcv: state TOKEN_FLAG, transitioning to TO %d\n", num);
#endif
			} else {
				//recieving gunk, pass period
				send_byte(control, num, '.'); //
			}

			break;

		case TO:

#ifdef DEBUG
        fprintf(stderr, "rcv: state TO, transitioning to FROM %d\n", num);
#endif

			if (sent) {
				rcv_state = FROM;
				send_byte(control, num, '.');
				break;
			}

			to = byte - 48;
			send_byte(control, num, byte);
			rcv_state = FROM;

			if (to == num) {
				WAIT_SEM(control, CRIT);
				control->shared_ptr->node[num].received++;
				SIGNAL_SEM(control, CRIT);
			}
			
			break;

		case FROM:


			if (!sent) {
				send_byte(control, num, byte);
			} else {
				send_byte(control,num, '.');
			}
			rcv_state = LEN;
			break;

		case LEN:

			len = (int) byte;


			if (!sent) {
				send_byte(control, num, byte);
			} else {
				send_byte(control,num, '.');
			}

			if (len > 0) {
				rcv_state = DATA;
			} else {
				rcv_state = TOKEN_FLAG;
				sent = 0;
			}

			break;

		case DATA:
			
			if (!sent) {
				send_byte(control, num, byte);
			} else {
				send_byte(control,num, '.');
			}

			--len;

			if (len == 0) {
				sent = 0;
				rcv_state = TOKEN_FLAG;
			}


			if (to == num) {
#ifdef DEBUG
				fprintf(stderr, "awdawdasd");
#endif
			}


			break;
		};
	}
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
	WAIT_SEM(control, CRIT);

	switch (control->snd_state) {
	case TOKEN_FLAG:
		sndpos = 0;
		sndlen = 0;
		control->snd_state = TO;
		SIGNAL_SEM(control, CRIT);
		send_byte(control, num, '+');

#ifdef DEBUG
        fprintf(stderr, "send_pkt: state TOKEN_FLAG, transitioning to TO\n");
#endif
		break;

	case TO:
		control->snd_state = FROM;
		SIGNAL_SEM(control, CRIT);

		send_byte(control, num, control->shared_ptr->node[num].to_send.to);
#ifdef DEBUG
        fprintf(stderr, "send_pkt: state TO, transitioning to FROM %d\n", control->shared_ptr->node[num].to_send.to);
#endif
		break;

	case FROM:
		control->snd_state = LEN;
		SIGNAL_SEM(control, CRIT);

		send_byte(control, num, control->shared_ptr->node[num].to_send.from);
#ifdef DEBUG
        fprintf(stderr, "send_pkt: state FROM, transitioning to LEN\n");
#endif
		break;

	case LEN:
		control->snd_state = DATA;
		sndlen = control->shared_ptr->node[num].to_send.length;
		sndpos = 0;
		SIGNAL_SEM(control, CRIT);
#ifdef DEBUG
        fprintf(stderr, "send_pkt: state LEN, transitioning to DATA\n");
#endif
		send_byte(control, num, sndlen);
		break;

	case DATA:


		if (sndpos >= sndlen) {
			control->snd_state = DONE;
			SIGNAL_SEM(control,CRIT);

		} else {
			SIGNAL_SEM(control,CRIT);
			send_byte(control, num, control->shared_ptr->node[num].to_send.data[sndpos]);
			sndpos++;
			break;
		}

		


		//count +1 sent once done

	case DONE:
		//reset
		WAIT_SEM(control, CRIT);
		sndpos = 0;
		sndlen = 0;
		control->shared_ptr->node[num].sent++;
		control->shared_ptr->node[num].to_send.token_flag = '1';
		SIGNAL_SEM(control, TO_SEND(num)); // this is to say you can regenerate data for this node


		SIGNAL_SEM(control,CRIT);
		send_byte(control, num, '/');

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

	int sendTo = (num + 1) % N_NODES;

	WAIT_SEM(control, EMPTY(sendTo));
	control->shared_ptr->node[sendTo].data_xfer = byte;
	SIGNAL_SEM(control, FILLED(sendTo));

#ifdef DEBUG
	fprintf(stderr, "sent byte:%d:%c from %d \n", byte,byte, num);
#endif
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


	WAIT_SEM(control, FILLED(num));
	byte = control->shared_ptr->node[num].data_xfer;
#ifdef DEBUG
				fprintf(stderr, "rec byte:%d:%c from %d\n", byte, byte, num);
#endif
	SIGNAL_SEM(control, EMPTY(num));

	return byte;

}

