#include "net_include.h"
#include "packets.h"
#include <time.h>
#include <stdbool.h>

#define MAX_MACHINES 10
#define WINDOW_SIZE 20
#define BURST 15

bool check_write(int arr[], int sz);
int get_min_index(int arr[], int sz);
//void print_grid(data_pkt **grid, int rows, int cols);

int main(int argc, char **argv)
{
    struct sockaddr_in name;
    struct sockaddr_in send_addr;
    int                mcast_addr;
    struct ip_mreq     mreq;
    unsigned char      ttl_val;

    int                ss,sr;
    fd_set             mask;
    fd_set             read_mask;
    int                bytes;
    int                num;
    //struct timeval     timeout;
    //char               mess_buf[MAX_MESS_LEN];

    int                num_packets;
    int                machine_index;
    int                num_machines;
    int                loss_rate;
    int                counter = 0;
    int                pkt_index = 0; 

    int                n;
    /*START*/
    
    /*handle arguments*/
    if ( argc != 5 ) {
        printf("Usage: mcast <num_packets> <machine_index> <num_machines> <loss_rate>\n");
        exit(0);
    }

    //can we assume that we won't have duplicate machines joining with the same machine index? 
    char *str = argv[1];
    num_packets = atoi(str);

    str = argv[3];
    num_machines = atoi(str);
    if ( num_machines > 10 || num_machines < 1 ) {
        printf("invalid number of machines\n");
        exit(1);
    }

    str = argv[2]; //machine index
    machine_index = atoi(str); //converts numeric str to int
    //must be in range 1 ... <num_machines>
    if ( machine_index > num_machines || machine_index < 1 ) { 
        printf("invalid machine_index\n");
        exit(1);
    }

    str = argv[4];
    loss_rate = atoi(str); //loss rate must be 0 ... 20
    
    printf("num_packets = %d, machine_index = %d, num_machines = %d, loss_rate = %d\n", num_packets, machine_index, num_machines, loss_rate);

    data_pkt*          received_pkts[WINDOW_SIZE][num_machines];
    int                write_arr[num_machines];
    int                acks_received[num_machines];
    int                lio_arr[num_machines]; //last in order array
    int                expected_pkt[num_machines]; //expected pkt index
    int                nack_counter[num_machines];

    /*create last_in_order array of size num_machines*/
    int last_in_order_arr[num_machines];

    //initialize excpected_pkt array
    for (int i = 0; i < num_machines; i++) {
        expected_pkt[i] = 1;
    }

    //initialize received_pkts
    int i,j;
    for (i = 0; i < WINDOW_SIZE; i++) {
       for (j = 0; j < num_machines; j++) {       
          received_pkts[i][j] = NULL;
        } 
    }

    mcast_addr = 225 << 24 | 0 << 16 | 1 << 8 | 1; /* (225.0.1.1) */
    //need 225.1.1.50
    sr = socket(AF_INET, SOCK_DGRAM, 0); /* socket for receiving */
    if (sr<0) {
        perror("Mcast: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );

    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, 
        sizeof(mreq)) < 0) 
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d \n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    FD_ZERO( &mask ); 
    FD_SET( sr, &mask );
    FD_SET( (long)0, &mask );    /* stdin */
    
    //cannot proceed until start_mcast is called & sends out a ~start signal 
    char start_buf[1];
    recv( sr, start_buf, sizeof(start_buf), 0);

    /*TRANSFER*/
    
    srand(time(NULL));

    //while loop - condition?
    for(;;) {
        //sending a packet send_packet():
        int burst = 15; 
        while ( burst > 0 && pkt_index < num_packets ) { 
            pkt_index++;
            counter++;
    
            //create header & a data_pkt 
            header data_head;
            data_head.tag = 0;
            data_head.machine_index = machine_index;
            data_pkt *new_pkt;
           
            new_pkt = malloc(sizeof(data_pkt));
            new_pkt->head = data_head; 
            new_pkt->pkt_index = pkt_index;
            new_pkt->counter = counter;
            /*init random num generator*/
            //srand(time(NULL));
            
            new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil
            //do we need to memcpy the array everytime?
            memcpy(new_pkt->acks, last_in_order_arr, sizeof(last_in_order_arr));

            //set new_pkt->payloads = random 1400 bytes?
        
            /*save the new_pkt in received_pkts grid*/
            received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;
            printf("\nstoring our packet(#%d) at [%d][%d]", pkt_index, pkt_index % WINDOW_SIZE, machine_index - 1);
            //if its the first ever packet being sent, set write_arr = 1 at its spot(**unless its final pkt ...)
            if ( pkt_index == 1 ) {
                write_arr[machine_index - 1] = 1; 
            }
            
            /* multicast/send the packet*/
            char buffer[sizeof(*new_pkt)]; //save the new data pkt into buffer before sending it
            memcpy(buffer, new_pkt, sizeof(*new_pkt)); //copies sizeof(new_pkt) bytes into buffer from new_pkt
            bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
            printf("Machine#%d sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num);
            printf("\nsizeof(new_pkt) = %ld\n", sizeof(new_pkt));
            printf("\naddress of new_pkt = %p\n", new_pkt);
            burst--;
        }

        //send final packet
        if ( pkt_index == num_packets ) {
            //send final pkt
            pkt_index++;
            final_pkt *final_msg;
            final_msg = malloc(sizeof(final_msg));
            header head;
            head.tag = 2;
            head.machine_index = machine_index;
            final_msg->head = head;
            final_msg->pkt_index = pkt_index;
            char buffer[sizeof(*final_msg)]; //save the final pkt into buffer before sending it
            memcpy(buffer, final_msg, sizeof(*final_msg));
            bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
            printf("\nSent final pkt: bytes = %d\n", bytes);
        }

        //print_grid(received_pkts, WINDOW_SIZE, num_machines);
        int i,j;
        for (i = 0; i < WINDOW_SIZE; i++) {
            for (j = 0; j < num_machines; j++) {
                if(received_pkts[i][j] == NULL) {
                    printf("[ ]");
                } else {
                    printf("[%d]", received_pkts[i][j]->rand_num);//, *received_pkts[i][j]);
                }
            }
            printf("\n");
        }
        
        //listen for incoming packets or timeout
        
        //how long do we want to be recieving pkts for? until we dont receive one in x amnt of time? possibly receiving in bursts too
        for(;;)
        {   
            read_mask = mask;
            //timeout.tv_sec = 1; //what do these 2 lines do?
            //timeout.tv_usec = 0;
            num = select( FD_SETSIZE, &read_mask, NULL, NULL, NULL); //&timeout); //event triggered
            if ( num > 0 ) { 
                if ( FD_ISSET( sr, &read_mask) ) { //recieved some type of packet  
                    char buf[sizeof(data_pkt)];
                    header *head;
                    bytes = recv( sr, buf, sizeof(buf), 0); //readinto a data_pkt by default  
                    head = (header*)buf;
                    
                    /* in the case that a machine received a packet from itself, ignore it*/ 
                    if ( head->machine_index == machine_index ) break;

                    printf("received header:\n\ttag = %d\n\tmachine_index = %d\n", head->tag, head->machine_index);

                    /* switch case based on head.tag */
                    
                    switch ( head->tag ) {
                        case 0: ; //data_pkt
                            data_pkt *pkt;
                            pkt = malloc(sizeof(data_pkt));
                            memcpy(pkt, buf, sizeof(data_pkt));
                            //pkt = (data_pkt*)buf;
                            printf("\ndata_pkt");
                            
                            /* check if we can store pkt */
    
                            if ( (n = lio_arr[head->machine_index]) < pkt->pkt_index && pkt->pkt_index < n + WINDOW_SIZE ) {
                                printf("\n\t%d < %d < %d ... so we can store it!\n", n, pkt->pkt_index, n + WINDOW_SIZE);
                                
                                //if the pkt_index == 1: we are receiving a proccess's first pkt (special case)
                                if ( pkt->pkt_index == 1 ) {
                                    //save its counter in write_arr
                                    write_arr[head->machine_index - 1] = pkt->counter;
                                }

                                //now, store it in our grid (received_pkts) 
                                received_pkts[pkt->pkt_index % WINDOW_SIZE][head->machine_index - 1] = pkt; 
                                printf("\n\tstoring the pkt into grid at [%d][%d]\n", pkt->pkt_index % WINDOW_SIZE, head->machine_index - 1);
                                /* hi Tsige, the code above stores it with a 1 offset like in project 1 (so first packet would go in the 1st index 
                                 *not 0th) ... we can always change that but thats how I set it for now 
                                 yup that looks good! - tsige*/
                            } else {
                                break;
                            }

                            //print_grid(received_pkts, WINDOW_SIZE, num_machines);
                            int i,j;
                            for (i = 0; i < WINDOW_SIZE; i++) {
                                for (j = 0; j < num_machines; j++) { 
                                    if(received_pkts[i][j] == NULL) {   
                                        printf("[ ]");
                                    } else { 
                                        printf("[%d]", received_pkts[i][j]->rand_num);//, *received_pkts[i][j]);
                                    }   
                                }   
                                    printf("\n");
                            }
                            /* now check if the pkt is in order */

                            if (pkt->pkt_index == (n = expected_pkt[head->machine_index - 1])) {
                                // packet is what we excpect
                                printf("packet index %d is in order\n", n);
                                expected_pkt[head->machine_index - 1]++;
                            } else if(pkt->pkt_index > n) {
                                // packets were missed
                                
                                // hey gen idk what you were thinkg since its not in the design 
                                // -- so I made the nack counter and array per machine
                                // we can talk about it tho if you think its unnecessary
                                
                                // increase 
                                nack_counter[head->machine_index - 1] += (pkt->pkt_index - n);
                                expected_pkt[head->machine_index - 1] = n+1;

                                printf("%d packet's are missed\n", nack_counter[head->machine_index - 1]);
                            } else {
                                // nack was received
                                nack_counter[head->machine_index - 1]--;
                                printf("nacked packet %d is recieved\n", pkt->pkt_index);
                            }

                            if (pkt->pkt_index == lio_arr[head->machine_index - 1] + 1) {
                                /* check if we can write */
                                //how do we know if we can start writing? -> we have atleast 1 msg from every proces in top row 
                                printf("\n\tChecking if we can write..."); 
                                printf("write_arr = %d, %d, %d", write_arr[0], write_arr[1], write_arr[2]);

                                //if there is a 0 in write_arr -> cannot write yet
                                //attempts to write
                                int min_machine = get_min_index( write_arr, num_machines );
                                while ( min_machine != -1 ) {
                                
                                    printf("\n\twriting ...\n");
                                   
                                    int col = min_machine;
                                    int row = (lio_arr[min_machine] + 1) % WINDOW_SIZE; //since its the last written we need to + 1

                                    data_pkt *write_pkt = received_pkts[row][col];
                                    printf("\ngoing to write pkt at index [%d][%d] to file\n", row, col);
                                    
                                    // TODO: this check isn't happening at the right time
                                    if(write_pkt == NULL) {
                                        printf("wait.. doesn't exist");
                                        break;
                                    }
                                    printf("\n...that packet contains: rand_num = %d\n", write_pkt->rand_num);

                                    //TODO: write it to the file
                                    //update the lio_arr 
                                    lio_arr[min_machine] = write_pkt->pkt_index;
                                    //we can set write_pkt to null, only if its not our machines msgs
                                    if ( col + 1 != machine_index ) {
                                        write_pkt = NULL;
                                        //not using min pointer anymore since we have a way of getting to the slot directly

                                        //finding the spot of the next minimum
                                        int next_row = (row + 1) % WINDOW_SIZE;
                                        write_pkt = received_pkts[next_row][col];
                                    
                                        //in case there is no pkt here ... done writing
                                        if ( write_pkt == NULL ) {
                                            printf("done writing\n");
                                            write_arr[col] = 0;                                     //need to make sure we update this... 
                                            min_machine = get_min_index( write_arr, num_machines );
                                        } else if ( write_pkt == (data_pkt*)-1 ) { //final pkt

                                        } else { //there is another pkt to write
                                            write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                            min_machine = get_min_index( write_arr, num_machines );
                                        }
                                        
                                        
                                    } else {
                                        printf("\n\tcannot write ...\n");
                                    }
                                }

                            }
                            /* check the pkt's acks --> we might be able to delete our packets from grid */

                            break;
                        case 1: ; //feedback
                            printf("\nfb_pkt");
                            //TODO: need to test handeling fb_pkt
                            feedback_pkt *fb_pkt;
                            fb_pkt = (feedback_pkt*)buf;
                            // checking number of nacks for our machine
                            int num_nacks = fb_pkt->nacks[0][machine_index-1]; 
                            
                            if (num_nacks > 0) {
                                // ressend nacks
                                for (int i = 1; i <= num_nacks; i++) {
                                    // get missing pkt_index
                                    int missing_index = fb_pkt->nacks[i][machine_index-1];
                                    data_pkt *missing_pkt = received_pkts[missing_index % WINDOW_SIZE][machine_index-1];

                                    // resend missing packet
                                    char buffer[sizeof(missing_pkt)];
                                    memcpy(buffer, missing_pkt, sizeof(*missing_pkt));
                                    bytes = sendto( ss, buffer, sizeof(buffer), 0,
                                                (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    printf("\nresent nack = %d\n", bytes);
                                }
                            }
                            // if process is done sending data_pkts then check acks (we have their final packet)
                            
                            if (write_arr[head->machine_index - 1] == -1) {
                                // check if ack is greater then current ack 
                                if (  (n = fb_pkt->acks[machine_index - 1]) > acks_received[head->machine_index -1]) {
                                        acks_received[head->machine_index - 1] = n;
                                }
                            }
                            break;
                        case 2: //final_pkt
                            printf("\nfinal_pkt");
                            final_pkt *fp = (final_pkt*)buf; 
                            /* check if we can store it yet */
                            if ( (n = lio_arr[head->machine_index]) < fp->pkt_index && fp->pkt_index < n + WINDOW_SIZE ) {
                                //TODO: set [machine_index - 1, pkt_index mod window] in received pkts = -1 (MARKS FINAL PKT) //refer design
                                received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1] = (data_pkt*)-1;
                                printf("\n\t%d < %d < %d ... so we can store it!\n", n, fp->pkt_index, n + WINDOW_SIZE);
                            }
            
    
                            break;  
                    } 
                     
                }   
            }   
        
            //timeout - break
        } 
        //send feedback if nack_counter > 0
    }
    return 0;
}

/* returns the index holding the minimum value (if there are 2+ with same value, the index most left is returned */
int get_min_index(int arr[], int sz) {
    int min_index = 0; 
    int i;
    for ( i = 0; i < sz; i++ ) {
        if ( arr[i] == 0 ) 
            return -1; //returns -1 if the arr contains a value 0  
        if ( arr[i] < arr[min_index] )  
            min_index = i; 
    } 
    return min_index;
}

/*
void print_grid(data_pkt **grid, int rows, int cols) {
    int i,j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            if(grid[i][j] == NULL) { 
                printf("[ ]");
            } else {
                printf("[%d]", grid[i][j].rand_num);
            }
        }
        printf("\n");
    }
}
*/
