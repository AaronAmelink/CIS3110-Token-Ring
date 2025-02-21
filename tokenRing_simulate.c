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
	int rcv_state = TOKEN_FLAG, not_done = 1, sending = 0, len, sent = 0, to, sendLen, from, dataLen;
	//token flag to my understanding is the "waiting to recieve stuff" flag
	//sent shows if I sent the first message I'm reading since sending. i.e. if I send a message, the first one I read next should be my own.
	unsigned char byte;
	char toBuffer[MAX_DATA+1]; // +\0
	int strPos = 0;

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
					//fprintf(stderr, "calling send packet %d\n", sendLen);
			#endif
			send_pkt(control, num);
			if (--sendLen == 0 ){
				sending = 0;
				sent = 1;
			}
		} 

		byte = rcv_byte(control, num);

		// if (sending) {
		// 	continue;
		// }


		/*
		 * Handle the byte, based upon current state.
		 */
		switch (rcv_state) {
		case TOKEN_FLAG:

			WAIT_SEM(control, CRIT);
			//are we done?
			if (control->shared_ptr->node[num].terminate) {
				not_done = 0;
				#ifdef DEBUG
					fprintf(stderr, "terminating %d\n", num);
				#endif
				SIGNAL_SEM(control, CRIT);
				send_byte(control, num, byte);
				break;
			}
			SIGNAL_SEM(control, CRIT);

			if (byte == '/') {
				//free to send data
				
				WAIT_SEM(control, CRIT);
				if (control->shared_ptr->node[num].to_send.token_flag == '0') {
					//we can send packet!

					sending = 1;
					control->snd_state = TOKEN_FLAG;
					sendLen = control->shared_ptr->node[num].to_send.length + 4; //to from len /
					
				} else {
					//free to send, but nothing to send from this node
					sending = 0;
				}
				SIGNAL_SEM(control, CRIT);


				if (!sending) {
					//nothing to send from here

					send_byte(control, num, byte);
					rcv_state = TOKEN_FLAG;

				}

			} else if (byte == '+') {
				//starting a new string

				if (!sent && !sending) {
					send_byte(control, num, byte);
				} else {
					#ifdef DEBUG
						fprintf(stderr, "DISCARDING %d %c\n", num, byte);
					#endif
				}
				rcv_state = TO;

			} else {
				//recieving gunk, pass period
				//send_byte(control, num, '.');
			}

			break;

		case TO:

			to = (int)byte;
			rcv_state = FROM;

			if (to == num) {
				WAIT_SEM(control, CRIT);
				control->shared_ptr->node[num].received++;
				SIGNAL_SEM(control, CRIT);
			}

			if (!sent && !sending) {
				send_byte(control, num, byte);
			} else {
				#ifdef DEBUG
					fprintf(stderr, "DISCARDING %d %c\n", num, byte);
				#endif
			}
			
			break;

		case FROM:

			from = (int)byte;

			if (!sent && !sending) {
				send_byte(control, num, byte);
			} else {
				#ifdef DEBUG
					fprintf(stderr, "DISCARDING %d %c\n", num, byte);
				#endif
			}
			rcv_state = LEN;
			break;

		case LEN:

			len = (int) byte;
			dataLen = (int) byte;


			if (!sent && !sending) {
				send_byte(control, num, byte);
			} else {
				#ifdef DEBUG
					fprintf(stderr, "DISCARDING %d %c\n", num, byte);
				#endif
			}

			if (len > 0) {
				rcv_state = DATA;
			} else {
				rcv_state = TOKEN_FLAG;
				sent = 0;
			}

			break;

		case DATA:
			if (!sent && !sending) {
				send_byte(control, num, byte);
			} else {
				#ifdef DEBUG
					fprintf(stderr, "DISCARDING %d %c\n", num, byte);
				#endif
			}

			--len;

			if (to == num) {

				toBuffer[strPos] = byte;
				strPos++;
			}

			if (len == 0) {
				if (sent && !sending) {
					SIGNAL_SEM(control, TO_SEND(num)); // this is to say you can regenerate data for this node. here because if sent something, program will exit if nothing else to send regardless of if sent packet has been recieved
				}
				sent = 0;
				rcv_state = TOKEN_FLAG;
				if (to == num) {
					toBuffer[strPos] = '\0';
					printf("RECIEVED DATA: %s \nFROM:%d\nTO:%d\nLEN:%d\n\n", toBuffer, from, to, dataLen);
					strPos = 0;
				}
				
			}

			break;
		};
	}
	#ifdef DEBUG
    	fprintf(stderr, "Node %d exiting\n", num);
    #endif
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
		#ifdef DEBUG
				fprintf(stderr, "STARTING PACKET SEND %d\n", num);
		#endif

		SIGNAL_SEM(control, CRIT);
		send_byte(control, num, '+');


		break;

	case TO:
		control->snd_state = FROM;

		send_byte(control, num, control->shared_ptr->node[num].to_send.to);
		SIGNAL_SEM(control, CRIT);

		break;

	case FROM:
		control->snd_state = LEN;
		send_byte(control, num, control->shared_ptr->node[num].to_send.from);

		SIGNAL_SEM(control, CRIT);


		break;

	case LEN:
		control->snd_state = DATA;
		sndlen = control->shared_ptr->node[num].to_send.length;
		sndpos = 0;
		send_byte(control, num, sndlen);

		SIGNAL_SEM(control, CRIT);

		break;

	case DATA:
		
		

		if (sndpos == sndlen-1) {
			send_byte(control, num, control->shared_ptr->node[num].to_send.data[sndpos]);
			control->shared_ptr->node[num].to_send.length = 0;
			control->snd_state = DONE;
			SIGNAL_SEM(control,CRIT);
			sndpos++;

		} else {
			send_byte(control, num, control->shared_ptr->node[num].to_send.data[sndpos]);
			SIGNAL_SEM(control,CRIT);
			sndpos++;
			break;
		}

		


		//count +1 sent once done

	case DONE:
		//reset
		sndpos = 0;
		sndlen = 0;
		control->shared_ptr->node[num].sent++;
		control->shared_ptr->node[num].to_send.token_flag = '1';
	
		#ifdef DEBUG
				fprintf(stderr, "FINISHED SENDING PACKET %d\n", num);
		#endif
		SIGNAL_SEM(control,CRIT);
		send_byte(control, num, '/');

		//SIGNAL_SEM(control, TO_SEND(num)); // this is to say you can regenerate data for this node

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
	#ifdef DEBUG
		fprintf(stderr, "WAITING TO SEND %d\n", num);
	#endif
	WAIT_SEM(control, EMPTY(sendTo));
	control->shared_ptr->node[sendTo].data_xfer = byte;
#ifdef DEBUG
	fprintf(stderr, "sent byte:%d:%c from %d \n", byte,byte, num);
#endif
	SIGNAL_SEM(control, FILLED(sendTo));


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
	#ifdef DEBUG
		fprintf(stderr, "WAITING TO RECIEVE %d \n", num);
	#endif

	WAIT_SEM(control, FILLED(num));
	byte = control->shared_ptr->node[num].data_xfer;
	#ifdef DEBUG
		fprintf(stderr, "rec byte:%d:%c from %d\n", byte, byte, num);
	#endif
	SIGNAL_SEM(control, EMPTY(num));

	return byte;

}

