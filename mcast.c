#include "net_include.h"
#include "packets.h"
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>


#define MAX_MACHINES 10
#define WINDOW_SIZE 20
#define RECV_THRESHOLD 10

bool check_write(int arr[], int sz);
int get_min_index(int arr[], int sz);
void print_grid(int rows, int cols, data_pkt *grid[rows][cols]); 
bool check_store(int write_arr[], int lio_arr[], data_pkt *pkt);
//void check_acks(data_pkt *pkt, int *acks_received, int machine_index);
int get_min_ack_received(int acks_received[], int machine_index, int num_machines);
bool only_min(int acks_received[], int machine_index, int num_machines);
bool exit_case(int acks_received[], int write_arr[], int num_machines, int num_packets);
void print_fb_pkt(feedback_pkt *fb, int num_machines);
int find_min_ack ( int *received_acks, int machine_index, int num_machines);
void fill_nacks(feedback_pkt *fb, int rows, int cols, data_pkt *grid[rows][cols], int *nack_counter, int *lio_arr, int *expected_pkt);

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
    struct timeval     timeout;
    //char               mess_buf[MAX_MESS_LEN];

    int                num_packets;
    int                machine_index;
    int                num_machines;
    int                loss_rate;
    int                counter = 0;
    int                pkt_index = 0; 
    int                received_count = 0;

    int                n, nwritten; 
    FILE               *fw; // pointer to dest file, which we write 
    
    /*handle arguments*/
    if ( argc != 5 ) {
        printf("Usage: mcast <num_packets> <machine_index> <num_machines> <loss_rate>\n");
        exit(0);
    }

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
    if ( machine_index > num_machines || machine_index < 1 ) { 
        printf("invalid machine_index\n");
        exit(1);
    }

    str = argv[4];
    loss_rate = atoi(str); //loss rate must be 0 ... 20
    if ( loss_rate > 20 || loss_rate < 0 ) {
        printf("invalid loss_rate\n");
        exit(1);
    }
    printf("num_packets = %d, machine_index = %d, num_machines = %d, loss_rate = %d\n", num_packets, machine_index, num_machines, loss_rate);

    data_pkt*          received_pkts[WINDOW_SIZE][num_machines];
    int                write_arr[num_machines];
    int                acks_received[num_machines];
    int                lio_arr[num_machines]; //last in order array
    int                expected_pkt[num_machines]; //expected pkt index
    int                nack_counter[num_machines];

    //initialize all our arrays
    for (int i = 0; i < num_machines; i++) {
        expected_pkt[i] = 1;
        acks_received[i] = 0;
        lio_arr[i] = 0;
        write_arr[i] = 0;
        nack_counter[i] = 0;
    }
    
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

    /* open or create the destination file for writing */
    char file_name[ sizeof(machine_index) ];
    sprintf ( file_name, "%d", machine_index ); //converts machine_index to a string 
    if ( (fw = fopen( (strcat(file_name,".txt") ) , "w") ) == NULL ) {
        perror("fopen");
        exit(0);
    }

    //cannot proceed until start_mcast is called 
    char start_buf[1];
    recv( sr, start_buf, sizeof(start_buf), 0 );

    /*TRANSFER*/
    
    /*init random num generator*/
    srand ( time(NULL) );

        //sending a packet send_packet():
        int burst = 10; //make sure burst < window size  
        while ( burst > 0 && pkt_index < num_packets && pkt_index < acks_received[machine_index - 1] + WINDOW_SIZE ) { 
            pkt_index++;
            counter++;
    
            data_pkt *new_pkt;
            new_pkt = malloc(sizeof(data_pkt));
            new_pkt->head.tag = 0; 
            new_pkt->head.machine_index = machine_index;
            new_pkt->pkt_index = pkt_index;
            new_pkt->counter = counter;

            new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil 

            //set new_pkt->payloads = random 1400 bytes?
            
            //save the new_pkt in received_pkts grid
            received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;
            //printf("\nstoring our packet(#%d) at [%d][%d]\n", pkt_index, pkt_index % WINDOW_SIZE, machine_index - 1);
            
            //if its the first ever packet being sent, set write_arr = 1 at its spot(**unless its final pkt ...)
            if ( pkt_index == 1 ) {
                write_arr[machine_index - 1] = 1; 
            }
            
            /* send the packet */
            //char buffer[sizeof(data_pkt)]; //save the new data pkt into buffer before sending it
            //memcpy(buffer, new_pkt, sizeof(data_pkt)); //copies sizeof(new_pkt) bytes into buffer from new_pkt
            bytes = sendto( ss, new_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
            
                printf("\nsent %d bytes\n", bytes); 
            printf("\nI(machine#%d) sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n\tcounter = %d", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num, new_pkt->counter);
            burst--;
        }

        //send final packet
        if ( pkt_index == num_packets ) {
            pkt_index++;
            //edge case: machine sends no data_pkts except a final pkt
            if ( pkt_index == 1 )  {
                write_arr[machine_index - 1] = -1;
                lio_arr[machine_index - 1] = 1;
            }
            
            data_pkt *final_msg;
            final_msg = malloc(sizeof(data_pkt));
            final_msg->head.tag = 2; //*head;
            final_msg->head.machine_index = machine_index; 
            final_msg->pkt_index = pkt_index;
            final_msg->counter = -1;
            final_msg->rand_num = -1;

            received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = final_msg; //(data_pkt*)final_msg;
            bytes = sendto( ss, final_msg, sizeof(data_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
            printf("\nSent final pkt: bytes = %d\n", bytes);
        }

        //listen for incoming packets or timeout
        
        print_grid(WINDOW_SIZE, num_machines, received_pkts);

        struct timeval t1;
        gettimeofday(&t1, NULL); //used to send fb packets every 6 sec
        for(;;)
        { 
            //case that we received xx amount of pkts --> send a fb pkt (later on, add the case we r missing x pkts)
            printf("\nwaiting for an event? ..\n");
            
            read_mask = mask;
            timeout.tv_sec = 1; 
            timeout.tv_usec = 0;
            num = select( FD_SETSIZE, &read_mask, NULL, NULL, &timeout); //event triggered
            if ( num > 0 ) 
            { 
                if ( FD_ISSET( sr, &read_mask) ) { //recieved some type of packet  
                    //received_count++; //does this increment also when we receive our OWN pkts (bc we ignore that in line 248)

                        //received into this buf every time
                    data_pkt * buf = malloc(sizeof(data_pkt)); 
                    //memset(buf, 0, sizeof(data_pkt));
                    //char buf[sizeof(data_pkt)];
                    header *head;
                    bytes = recv( sr, buf, sizeof(data_pkt), 0); 
                    head = (header*) buf;
                        printf("recieved %d bytes\n", bytes); 
                    /* in the case that a machine received a packet from itself, ignore it*/ 
                    if ( head->machine_index == machine_index ) continue; 

                    printf("received header:\n\ttag = %d\n\tmachine_index = %d\n", head->tag, head->machine_index);
                    received_count++;
                    /* switch case based on head.tag */
                    
                    switch ( head->tag ) {
                        case 0: ; //data_pkt
                            data_pkt *pkt = buf;
                            if (pkt == NULL) printf("ERRORRR\n`");
                            else printf("existenxe\n");

                                printf ("from machine #%d\t pkt_index%d\t random_num%d", pkt->head.machine_index, pkt->pkt_index, pkt->rand_num);
                            //pkt = malloc(sizeof(data_pkt));
                            //memcpy(pkt, buf, sizeof(data_pkt));
                            
                            printf("\n(data_pkt)\n");
                            
                            printf("lio_arr = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]); 
                            /* check if we can store pkt */
                            if ( received_pkts[pkt->pkt_index % WINDOW_SIZE][head->machine_index - 1] != NULL || !check_store(write_arr, lio_arr, pkt) ) {
                                continue;
                            }
                            
                            //if the pkt_index == 1: we are receiving a proccess's first pkt (special case)
                            if ( pkt->pkt_index == 1 ) {
                                //save its counter in write_arr
                                write_arr[head->machine_index - 1] = pkt->counter;
                            }

                            //now, store it in our grid (received_pkts) 
                            received_pkts[pkt->pkt_index % WINDOW_SIZE][head->machine_index - 1] = pkt; 
                            printf("\n\tstoring the pkt into grid at [%d][%d]\n", pkt->pkt_index % WINDOW_SIZE, head->machine_index - 1);

                            print_grid(WINDOW_SIZE, num_machines, received_pkts); 
                            

                             //checks if we can adopt the pkts counter
                            if (pkt->counter > counter ) {
                                counter = pkt->counter;
                                printf("\nUpdated our counter = %d\n", counter);
                            }

                            /* now check if the pkt is in order */

                            if (pkt->pkt_index == (n = expected_pkt[head->machine_index - 1])) {
                                // packet is what we excpect
                                expected_pkt[head->machine_index - 1]++;
                            } else if(pkt->pkt_index > n) { 
                                // packets were missed
                                
                                nack_counter[head->machine_index - 1] += (pkt->pkt_index - n);
                                printf("updated nack counter for machine %d to %d nacks", head->machine_index,nack_counter[head->machine_index - 1]) ;
                                expected_pkt[head->machine_index - 1] = pkt->pkt_index + 1;
                                printf("\t expected index:  %d \n", expected_pkt[head->machine_index - 1]) ;

                                printf("\n\t\t\tNACK_WINDOW = %d\n\n", NACK_WINDOW);
                                if (nack_counter[head->machine_index - 1] >= NACK_WINDOW - 1) {
                                        // send feedback
                                        //header *fb_head;
                                        //fb_head = malloc(sizeof(header));
                                        //fb_head->tag = 1; //FB_PKT;
                                        //fb_head->machine_index = machine_index;
                                        feedback_pkt *fb;
                                        fb = malloc(sizeof(feedback_pkt));
                                        fb->head.tag = 1;//*fb_head;
                                        fb->head.machine_index = machine_index;
                                        memcpy(fb->acks, lio_arr, sizeof(lio_arr));
                                        fill_nacks(fb, WINDOW_SIZE, num_machines, received_pkts, nack_counter, lio_arr, expected_pkt); 
                                        
                                        printf("fb acks = ");
                                        for(i=0;i<num_machines;i++) printf(" %d ",fb->acks[i]); 
                                        printf("fb nacks = ");
                                        for(i=0;i<num_machines;i++) printf(" %d ",fb->nacks[0][i]); 

                                        //char buffer[sizeof(feedback_pkt)];
                                        //memcpy(buffer, fb, sizeof(*fb));
                                        nwritten = sendto( ss, fb, sizeof(feedback_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                        printf("\n\nSENT A FB PKT of size = %d\n\n", nwritten);
                                        print_fb_pkt(fb, num_machines);
                                }

                                printf("%d packet's are missed\n", nack_counter[head->machine_index - 1]); 
                            
                            } else {
                                // nack was received
                                nack_counter[head->machine_index - 1]--;
                                printf("nacked packet %d is recieved\n", pkt->pkt_index);
                            }

                            printf("lio_arr = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]); 
                    
                            /* check if we can write */
                 
                            if (pkt->pkt_index == lio_arr[head->machine_index - 1] + 1) {
                                //if we just got the msg that's +1 from lio this means we might have a 0 in write_arr, need to update it
                                write_arr[head->machine_index - 1] = pkt->counter;

                                //we can start writing when we have atleast 1 msg from every proces in top row 
                                printf("\n\tChecking if we can write...write_arr = ");
                                for(i = 0; i < num_machines; i++) printf("%d ", write_arr[i]);
                                
                                //Attempting to write to file, if there is a 0 in write_arr, we cant write  
                                int min_machine; // = get_min_index( write_arr, num_machines );
                                while ( ( min_machine = get_min_index( write_arr, num_machines ) ) != -2 && (write_arr[min_machine] != -1)) { 
                                    printf("\n\twriting ...\n");
                                   
                                    int col = min_machine;
                                    int row = (lio_arr[min_machine] + 1) % WINDOW_SIZE; //since its the last written we need to + 1
                                    data_pkt *write_pkt = received_pkts[row][col];
                                    
                                    if(write_pkt == NULL) printf("\n376 NULL\n");
                                    //update the lio_arr 
                                    lio_arr[min_machine] = write_pkt->pkt_index;
  
                                    /* write it to the file */
                                    char buf_write[sizeof(write_pkt->rand_num)];
                                    sprintf(buf_write, "%d", write_pkt->rand_num); //converts int to a str before writing it
                                    fprintf(fw, "%2d, %8d, %8d\n", write_pkt->head.machine_index, write_pkt->pkt_index, write_pkt->rand_num);
                                    fflush(fw);

                                    printf("lio_arr = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]);
                                    printf("\tacks_received = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                                    //we can set write_pkt to null, only if its not our machines msgs
                                    if ( col + 1 != machine_index ) {
                                        //free(write_pkt->head);
                                        free(write_pkt);
                                        write_pkt = NULL; 
                                        received_pkts[row][col] = NULL;
                                    }

                                    printf("\n");
                                    print_grid(WINDOW_SIZE, num_machines, received_pkts);
                                    
                                    //finding the spot of the next minimum
                                    int next_row = (row + 1) % WINDOW_SIZE;
                                    write_pkt = received_pkts[next_row][col];
                                
                                    //in case there is no pkt here ... done writing
                                    if ( write_pkt == NULL ) {
                                        printf("done writing\n");
                                        write_arr[col] = 0; 
                                    } else { //there is another pkt to write
                                        write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                        
                                        if (write_pkt->counter == -1) {
                                            lio_arr[col] = write_pkt->pkt_index;
                                        }
                                    }
                                        
                                }

                                if (min_machine != -2 && write_arr[min_machine] == -1) {
                                        //possibliity of being done
                                        if ( exit_case(acks_received, write_arr, num_machines, num_packets) ) return 0;
                                }

                            }

                            break;
                        case 1: ; //feedback
                            printf("\ncase: feedback_pkt\n");
                            feedback_pkt *fb_pkt; 
                            fb_pkt = (feedback_pkt*)buf;
                            print_fb_pkt(fb_pkt, num_machines);
                            //check the feedback pkts acks -> might be able to delete some of our pkts 
                            int new_ack = fb_pkt->acks[machine_index - 1];
                       
                            //when we see that a pkt's ack = our min ack (lio_arr)
                            //then send the first two pkts again
                            if ( fb_pkt->acks[machine_index-1] == acks_received[machine_index-1] ) 
                            {
                                //send last pkt
                                data_pkt *temp = received_pkts[pkt_index % WINDOW_SIZE][machine_index-1]; 
                                char buffer[sizeof(data_pkt)];
                                if(temp == NULL) printf("\nNULL\n");
                                printf("\ntag = %d, pkt_index = %d\n", temp->head.tag, temp->pkt_index);

                                memcpy(buffer, temp, sizeof(data_pkt));
                                bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                printf("\nre sent our last pkt bc of inactivity\n");
                            }

                            printf("\tbefore acks_received = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                            printf("\nnew_ack = %d", new_ack);
                            if ( new_ack > acks_received[fb_pkt->head.machine_index - 1] ) { 
                                acks_received[fb_pkt->head.machine_index - 1] = new_ack;
                                int acks_min = find_min_ack(acks_received, machine_index, num_machines);
                                printf("\nget_min_ack = %d", acks_min);
                                if (acks_min == acks_received[machine_index - 1] ) { //cant shift
                                    printf("\ncannot shift our window");
                                } else {
                                    printf("\nwe can shift to %d\n", acks_min);
                                    int old_ack = acks_received[machine_index - 1];
                                    acks_received[machine_index - 1] = acks_min; //update the total ack
                                    printf("\n\tour updated acks_received = ");
                                    for(int i=0;i<num_machines;i++) printf(" %d ", acks_received[i]);
                                    /* shifts the window by deleting packets */
                                    int shift_by = acks_min - old_ack; //number of packets we can shift by & delete 
                                    printf("\tshift_by = %d\n", shift_by);

                                    for ( int i = 1; i <= shift_by; i++ ) {
                                        int delete_pkt_index = old_ack + i;
                                        data_pkt *temp = received_pkts[ (delete_pkt_index % WINDOW_SIZE) ][machine_index - 1];
                                        //free(head);
                                        free(temp);
                                        temp = NULL;
                                        received_pkts[ (delete_pkt_index % WINDOW_SIZE) ][machine_index - 1] = NULL;
                                        printf("\n...deleted pkt in [%d][%d]\n", delete_pkt_index % WINDOW_SIZE, machine_index - 1);
                                    }
                                    //if we deleted the FP - update LIO
                                    if (old_ack + shift_by == num_packets + 1) {
                                        lio_arr[machine_index-1] = num_packets + 1;
                                        printf("\n");
                                        print_grid(WINDOW_SIZE, num_machines, received_pkts);
                                        if(exit_case(acks_received, write_arr, num_machines, num_packets)) return 0;
                                    }
                                    printf("\nDONE SHIFTING WINDOW\n");
                                }
                            }

                            printf("\tafter acks_received = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);

                            int num_nacks = fb_pkt->nacks[0][machine_index-1]; 
                            printf("\nnum_nacks they are missing from us = %d\n", num_nacks); 
                            if (num_nacks > 0) {
                                printf("\nresending nacks\n");
                                // ressend nacks
                                for (int i = 1; i <= num_nacks; i++) {
                                    // get missing pkt_index
                                    int missing_index = fb_pkt->nacks[i][machine_index-1];
                                    data_pkt *missing_pkt = received_pkts[missing_index % WINDOW_SIZE][machine_index-1];

                                    // resend missing packet
                                    char buffer[sizeof(data_pkt)];
                                    memcpy(buffer, missing_pkt, sizeof(data_pkt));
                                    bytes = sendto( ss, buffer, sizeof(buffer), 0,
                                                (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    printf("\nresent nack = %d\n", missing_pkt->pkt_index);
                                }
                            }
                            break;
                        case 2: ; //final_pkt
                            printf("\n(final_pkt)\n");
                            data_pkt *fp;
                            fp = buf; 
                            
                            /* check if we can store it yet */
                            if ( received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1] != NULL || !check_store(write_arr, lio_arr, fp) ) {
                                continue;
                            }

                             // checking if in order
                             if (fp->pkt_index == (n = expected_pkt[head->machine_index - 1])) {
                                // packet is what we excpect
                                expected_pkt[head->machine_index - 1]++;
                            } else if(fp->pkt_index > n) { 
                                // packets were missed
                                
                                nack_counter[head->machine_index - 1] += (fp->pkt_index - n);
                                printf("updated nack counter for machine %d to %d nacks", head->machine_index,nack_counter[head->machine_index - 1]) ;
                                expected_pkt[head->machine_index - 1] = fp->pkt_index + 1;
                                printf("\t expected index:  %d \n", expected_pkt[head->machine_index - 1]) ;
                                
                                printf("\n\t\t\tNACK_WINDOW = %d\n\n", NACK_WINDOW);
                                if (nack_counter[head->machine_index - 1] >= NACK_WINDOW - 1) {
                                        // send feedback
                                        header *fb_head;
                                        fb_head = malloc(sizeof(header));
                                        fb_head->tag = 1; //FB_PKT;
                                        fb_head->machine_index = machine_index;
                                        feedback_pkt *fb;
                                        fb = malloc(sizeof(feedback_pkt));
                                        fb->head = *fb_head;
                                        memcpy(fb->acks, lio_arr, sizeof(lio_arr));
                                        fill_nacks(fb, WINDOW_SIZE, num_machines, received_pkts, nack_counter, lio_arr, expected_pkt); 
                                        
                                        printf("fb acks = ");
                                        for(i=0;i<num_machines;i++) printf(" %d ",fb->acks[i]); 
                                        printf("fb nacks = ");
                                        for(i=0;i<num_machines;i++) printf(" %d ",fb->nacks[0][i]); 

                                        char buffer[sizeof(feedback_pkt)];
                                        memcpy(buffer, fb, sizeof(feedback_pkt));
                                        nwritten = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                        print_fb_pkt(fb, num_machines);
                                }
                                printf("%d packet's are missed\n", nack_counter[head->machine_index - 1]);
                            } else {
                                // nack was received
                                nack_counter[head->machine_index - 1]--;
                                printf("nacked packet %d is recieved\n", fp->pkt_index);
                            }

                            /* check if we can write */
                 
                            //for a final pkt: if it's 1 bigger than LIO, we have received all pkts from this machine including its final pkt
                                    //other case is that we can store the final pkt but we r missing info before it/havent written yet so just store it
                            if ( lio_arr[head->machine_index - 1] == fp->pkt_index - 1 ) { //also handles machines that dont send any data pkts
                                write_arr[head->machine_index - 1] = -1;
                                lio_arr[head->machine_index - 1] = fp->pkt_index;
                                received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1] = fp;
                            } else {
                                //just store the final pkt
                                received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1] = fp;
                                printf("done storing the final pkt\nwrite_arr = ");
                                for(i = 0; i < num_machines; i++) printf("%d ", write_arr[i]);
                            }
                                //Attempting to write to file, if there is a 0 in write_arr, we cant write  
                                int min_machine;
                                while ( ( min_machine = get_min_index( write_arr, num_machines ) ) != -2 && (write_arr[min_machine] != -1)) 
                                {                                    
                                    printf("\n\t524: the final pkt told us we can cont writing ...\n");
                                    int col = min_machine;
                                    int row = (lio_arr[min_machine] + 1) % WINDOW_SIZE; //since its the last written we need to + 1
                                    data_pkt *write_pkt = received_pkts[row][col];

                                    //update the lio_arr 
                                    lio_arr[min_machine] = write_pkt->pkt_index;
                                    
                                    /* write it to the file */
                                    char buffer[sizeof(write_pkt->rand_num)];
                                    sprintf(buffer, "%d", write_pkt->rand_num); //converts int to a str before writing it
                                    fprintf(fw, "%2d, %8d, %8d\n", write_pkt->head.machine_index, write_pkt->pkt_index, write_pkt->rand_num);
                                    fflush(fw);

                                    printf("lio_arr = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]);
                                    printf("\tacks_received = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                                    //we can set write_pkt to null, only if its not our machines msgs
                                    if ( col + 1 != machine_index ) {
                                        //we can delete the packet and set it = null & check out the next address
                                        //free(write_pkt->head);
                                        free(write_pkt);
                                        write_pkt = NULL; 
                                        received_pkts[row][col] = NULL;
                                    }

                                    printf("\n");
                                    print_grid(WINDOW_SIZE, num_machines, received_pkts);
                                    
                                    //finding the spot of the next minimum
                                    int next_row = (row + 1) % WINDOW_SIZE;
                                    write_pkt = received_pkts[next_row][col];
                                
                                    //in case there is no pkt here ... done writing
                                    if ( write_pkt == NULL ) {
                                        printf("done writing\n");
                                        write_arr[col] = 0;
                                    } else { //there is another pkt to write
                                        write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                        if (write_pkt->counter == -1) {
                                            lio_arr[col] = write_pkt->pkt_index;
                                        }
                                    }
                                }
                                if (min_machine != -2 && write_arr[min_machine] == -1) {
                                        //possibliity of being done
                                        if ( exit_case(acks_received, write_arr, num_machines, num_packets) ) return 0;
                                }
                            break;  
                    }
                    print_grid(WINDOW_SIZE, num_machines, received_pkts);
                }   
            } else {

                printf("\nreceived_count = %d\n", received_count);
                printf("\n ...timeout");
                fflush(0);
                
                /* SEND FB PKT HERE */
                    //header *fb_head;
                    //fb_head = malloc(sizeof(header));
                    //fb_head->tag = 1; //FB_PKT;
                    //fb_head->machine_index = machine_index;
                    feedback_pkt *fb;
                    fb = malloc(sizeof(feedback_pkt));
                    fb->head.tag = 1; 
                    fb->head.machine_index = machine_index;
                    memcpy(fb->acks, lio_arr, sizeof(lio_arr));
                    fill_nacks(fb, WINDOW_SIZE, num_machines, received_pkts, nack_counter, lio_arr, expected_pkt); 
                
                    printf("fb acks = ");
                    for(i=0;i<num_machines;i++) printf(" %d ",fb->acks[i]); 
                    printf("fb nacks = ");
                    for(i=0;i<num_machines;i++) printf(" %d ",fb->nacks[0][i]); 

                    //char buffer[sizeof(feedback_pkt)];
                    //memcpy(buffer, fb, sizeof(*fb));
                    nwritten = sendto( ss, fb, sizeof(data_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    print_fb_pkt(fb, num_machines);
            }

            /*check our recieved_count and recv_threshold to determine if we need to send a burst OR if a certain time passed */
            int timer = 5;
            struct timeval curr_time;
            gettimeofday(&curr_time, NULL);
            printf("\ntime difference in mili sec= %ld\n", (curr_time.tv_usec - t1.tv_usec)/1000);
            if ( (received_count%RECV_THRESHOLD == RECV_THRESHOLD - 1) || ((curr_time.tv_usec - t1.tv_usec)/1000) > timer ) {
                // send a burst
                int burst = 2; //make sure burst < window size  
                while ( burst > 0 && pkt_index < num_packets && pkt_index < acks_received[machine_index - 1] + WINDOW_SIZE ) 
                { 
                    pkt_index++;
                    counter++;
            
                    //create header & a data_pkt 
                    header *data_head;
                    data_head = malloc(sizeof(header));
                    data_head->tag = 0;
                    data_head->machine_index = machine_index;
                    data_pkt *new_pkt;
                   
                    new_pkt = malloc(sizeof(data_pkt));
                    new_pkt->head = *data_head; 
                    new_pkt->pkt_index = pkt_index;
                    new_pkt->counter = counter;

                    new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil 

                    /*save the new_pkt in received_pkts grid*/
                    received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;
                    
                    /* send the packet */
                    char buffer[sizeof(data_pkt)]; //save the new data pkt into buffer before sending it
                    memcpy(buffer, new_pkt, sizeof(data_pkt)); //copies sizeof(new_pkt) bytes into buffer from new_pkt
                    bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
                    printf("\nI(machine#%d) sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n\tcounter = %d", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num, new_pkt->counter);
                    burst--;
                }

                //send final packet
                if ( pkt_index == num_packets ) {
                    pkt_index++;
                    data_pkt *final_msg;
                    final_msg = malloc(sizeof(data_pkt));
                    header *head;
                    head = malloc(sizeof(header));
                    head->tag = 2;
                    head->machine_index = machine_index;
                    final_msg->head = *head;
                    final_msg->pkt_index = pkt_index;
                    final_msg->counter = -1;
                    char buffer[sizeof(data_pkt)]; //save the final pkt into buffer before sending it
                    memcpy(buffer, final_msg, sizeof(data_pkt));
                    bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    printf("\nSent final pkt: bytes = %d\n", bytes);
                    received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = final_msg;
                }

                feedback_pkt *fb;
                fb = malloc(sizeof(feedback_pkt));
                fb->head.tag = 1; //*fb_head;
                fb->head.machine_index = machine_index; //*fb_head;
                
                //should we initi every value in acks to null?or 0? & for nacks
                memcpy(fb->acks, lio_arr, sizeof(lio_arr));
                
                fill_nacks(fb, WINDOW_SIZE, num_machines, received_pkts, nack_counter, lio_arr, expected_pkt); 
                
                printf("fb acks = ");
                for(i=0;i<num_machines;i++) printf(" %d ",fb->acks[i]); 
                printf("fb nacks = ");
                for(i=0;i<num_machines;i++) printf(" %d ",fb->nacks[0][i]); 

                char buffer[sizeof(feedback_pkt)];
                memcpy(buffer, fb, sizeof(feedback_pkt));
                nwritten = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                printf("\n\nSENT A FB PKT of size = %d\n\n", nwritten);
                print_fb_pkt(fb, num_machines);

                t1 = curr_time;
                //gettimeofday(&t1,NULL);
            }
        }
    return 0;
}


/* returns the index holding the minimum value (if there are 2+ with same value, the index most left is returned */
int get_min_index(int arr[], int sz) {
    int min_index = 0; 
    int i;
    int neg_count = 0;

    for(int i =0;i<sz;i++) printf(" %d ", arr[i]);
    for ( i = 0; i < sz; i++ ) {
        if ( arr[min_index] == -1 ) { //adopt the next index
          min_index = i;
        }
        if ( arr[i] == 0 ) 
            return -2; //returns -2 if the arr contains a value 0  
        if ( arr[i] != -1 && arr[i] < arr[min_index] )  
            min_index = i;  
    }
    printf("\nget_min_index is returning = %d\n", min_index);
    if(neg_count == sz) min_index = -1; //incase they are all -1
    return min_index;

}

/*takes a pointer to a 2d arr grid, its num of rows and cols */
void print_grid(int rows, int cols, data_pkt *grid[rows][cols]) {
    int i,j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            if(grid[i][j] == NULL) { 
                printf("[ ]");
            } else if (grid[i][j]->counter == -1) {
                printf("[FP]");
            } else {
                //trying to access rand_num when it's not empty
                printf("[%d]", grid[i][j]->rand_num);
            }
        }
        printf("\n");
    }
}

bool check_store(int write_arr[], int lio_arr[], data_pkt *pkt) {
    int n;
    
    if ( (write_arr[pkt->head.machine_index - 1]) != -1 && (n = lio_arr[pkt->head.machine_index - 1]) < pkt->pkt_index && pkt->pkt_index < n + WINDOW_SIZE ) { 
        printf("\n\t%d < %d < %d -> so we can store it!\n", n, pkt->pkt_index, n + WINDOW_SIZE);
        return true;
    }
    return false;
}

/* checks to see whether any of the other machines have the same min ack meaning we can't shift the window */
bool only_min(int acks_received[], int machine_index, int num_machines) {
    int min_ack = acks_received[machine_index]; //the current min ack of our machine
    for(int i = 0; i < num_machines; i++) {
        if ( !(i == machine_index - 1) ) {
            if ( acks_received[i] == min_ack ) return false; //another machine has the same min ack
        }
    }
    return true;
}

/* implement get_min_ack_received */
int get_min_ack_received(int acks_received[], int machine_index, int num_machines) { 
    acks_received[machine_index - 1] = -1; // so that the current min ack will be ignored
    
    int ret_min = get_min_index(acks_received, num_machines ); //will return -1 if all of them = -1 or the min pkt to shift to 
    acks_received[machine_index-1] = acks_received[ret_min];
    return acks_received[machine_index-1];
}

//we can exit if all ints in write_arr = -1, meaning every msg from every machine is written && acks_received are all -1,
//meaning every other machine has confirmed they received my final pkt
bool exit_case(int acks_received[], int write_arr[], int num_machines, int num_packets) {
    printf("\n\texit case called ... cheking if we can exit\n");
    for (int i = 0; i < num_machines; i++) {
        if ( acks_received[i] != num_packets + 1 || write_arr[i] != -1 ) 
            return false; //something is not finished -> cannot exit
    }
    return true;
}

void fill_nacks(feedback_pkt *fb, int rows, int cols, data_pkt *grid[rows][cols], int *nack_counter, int *lio_arr, int *expected_pkt) {
    printf("\nFILLING FB->NACKS\n");

    // initalizing fb nacks grid POSSIBLY MAKE ONE FB PACKET THATS PRE INITALIZED
    for (int i = 0; i < cols; i++) {
        for (int j = 0; j < NACK_WINDOW; j++) {
                fb->nacks[j][i] = 0;
        }
    }

    int lio; // last in order
    int nacks_found;
    int nack_count;
    int max_received;

    //iterate through each machine's column to find nacks
    // only till the excpected pkt_index num
    for (int i = 0; i < cols; i++) {
        lio = lio_arr[i];
        nacks_found = 0;
        nack_count = nack_counter[i];
        if (nack_count > NACK_WINDOW-1) {
                nack_count = NACK_WINDOW-1;
        }

        fb->nacks[0][i] = nack_count;
        max_received = expected_pkt[i] - 1;
        for (int j = lio+1; j < max_received && nacks_found <= nack_count; j++) {
                int pkt_index = j;
                if (grid[pkt_index%WINDOW_SIZE][i] == NULL) {
                        nacks_found++;
                        fb->nacks[nacks_found][i] = pkt_index; //jump to last nack in column and add the missing pkt value
                }
        }
    }
    printf("feedback nack grid\n");
 for (int i = 0; i < cols; i++) {
        for (int j = 0; j < NACK_WINDOW; j++) {
                printf(" %d ", fb->nacks[j][i]);
        }
        printf("\n");
    }

}

void print_fb_pkt(feedback_pkt *fb, int num_machines) {
    printf("\theader: tag = %d, machine_index = %d, nacks = not handled yet\n", fb->head.tag, fb->head.machine_index);
    printf("\tacks = ");
    for(int i = 0; i < num_machines; i++) printf(" %d ", fb->acks[i]);
}

/* returns the value of the smallest ack */
int find_min_ack ( int *received_acks, int machine_index, int num_machines) {
    int min = -1;
    printf("\nfind_min_ack():\n");
    printf("\treceived_acks = ");
    for(int z = 0; z < num_machines; z++) printf(" %d ", received_acks[z]);
    for(int i = 0; i < num_machines; i++) {
        //we skip our own index & any -1's
        if ( i != (machine_index - 1) && received_acks[i] != -1 ) {
            //cannot shift bc a pkt hasn't received anything grater than our last ack
            if ( received_acks[i] == received_acks[machine_index - 1] ) return received_acks[machine_index - 1];
            if ( min == -1 ) min = received_acks[i]; //when it's the first valid value, adopt it
            else if ( received_acks[i] < min ) min = received_acks[i];
        }
    }
    printf("\treturning min = %d\n", min);
    return min;
}

