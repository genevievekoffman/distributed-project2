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
int get_min_ack_received(int acks_received[], int machine_index, int num_machines);
bool only_min(int acks_received[], int machine_index, int num_machines);
bool exit_case(int acks_received[], int write_arr[], int num_machines, int num_packets);
void print_fb_pkt(feedback_pkt *fb, int num_machines);
int find_min_ack ( int *received_acks, int machine_index, int num_machines);
void fill_nacks(feedback_pkt *fb, int rows, int cols, data_pkt *grid[rows][cols], int *nack_counter, int *lio_arr, int *expected_pkt);
void clean_exit(struct sockaddr_in *send_addr, int num_machines, int *lio_arr, int machine_index, int ss);
feedback_pkt *send_feedback(struct sockaddr_in *send_addr, int num_machines, data_pkt *received_pkts[WINDOW_SIZE][num_machines], int *lio_arr, int machine_index, int *nack_counter, int *expected_pkt, int ss);

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

    int                num_packets;
    int                machine_index;
    int                num_machines;
    int                loss_rate;
    int                counter = 0;
    int                pkt_index = 0; 
    int                received_count = 0;

    int                n, nwritten; 
    FILE               *fw; // pointer to dest file, which we write 
    
    /* handle arguments */
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

    str = argv[2]; 
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
    int                expected_pkt[num_machines]; 
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

    //creates the destination file for writing
    char file_name[ sizeof(machine_index) ];
    sprintf ( file_name, "%d", machine_index ); //converts machine_index to a string 
    if ( (fw = fopen( (strcat(file_name,".txt") ) , "w") ) == NULL ) {
        perror("fopen");
        exit(0);
    }

    //cannot proceed until start_mcast is called 
    char start_buf[1];
    recv( sr, start_buf, sizeof(start_buf), 0 );
    
    srand ( time(NULL) ); //init random num generator
 
    /* TRANSFER */
    int burst = (int) WINDOW_SIZE * 0.1; // 10% of out window                 
    while ( burst > 0 && pkt_index+1 <= num_packets+1 && pkt_index+1 <= acks_received[machine_index - 1] + WINDOW_SIZE ) { 
        pkt_index++;

        data_pkt *new_pkt;
        new_pkt = malloc(sizeof(data_pkt));
        new_pkt->head.machine_index = machine_index;
        new_pkt->pkt_index = pkt_index;

        if (pkt_index == num_packets + 1) {
                new_pkt->head.tag = 2; 
                new_pkt->counter = -1;
                new_pkt->rand_num = -1; 
        } else {
                new_pkt->head.tag = 0; 
                counter++;
                new_pkt->counter = counter;
                new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil 
        }
        //set new_pkt->payloads = random 1400 bytes?
        //memset(new_pkt->payload, 0, sizeof(int)*350);

        //save new_pkt in our received_pkts grid
        received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;
        
        //if its the first packet being sent in the window, update write arr
        if ( write_arr[machine_index - 1] == 0 ) 
            write_arr[machine_index - 1] = pkt_index; 
        
        //sends the packet
        bytes = sendto( ss, new_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
         
        //printf("\npacket size = %ld\n", sizeof(*new_pkt)); //our size is 5620 bytes ... why is it sm bigger than 1412?
        printf("\nI(machine#%d) sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n\tcounter = %d", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num, new_pkt->counter);
        burst--;
    }
        
    //edge case: machine sends no data pkts except final pkt
    if ( pkt_index == 1 && num_packets == 0 ) {
        write_arr[machine_index - 1] = -1;
        lio_arr[machine_index - 1] = 1;
    }

        /* listens for incoming packets or timeout */
        print_grid(WINDOW_SIZE, num_machines, received_pkts);
        struct timeval t1;
        gettimeofday(&t1, NULL); //used to send fb packets every 6 sec
        
        for(;;)
        { 
            printf("\nwaiting for an event? ..\n");
            read_mask = mask;
            timeout.tv_sec = 1; 
            timeout.tv_usec = 0;
            num = select( FD_SETSIZE, &read_mask, NULL, NULL, &timeout); //event triggered
            if ( num > 0 ) 
            { 
                if ( FD_ISSET( sr, &read_mask) ) { //recieved some type of packet  

                    data_pkt *buf = malloc(sizeof(data_pkt)); //receive into this buf every time 
                    bytes = recv( sr, buf, sizeof(data_pkt), 0); 
                    header *head;
                    head = (header*) buf;
                    
                    //in case a machine receives a packet from itself, ignore it
                    if ( head->machine_index == machine_index ) continue; 

                    printf("received pkt with header:\n\ttag = %d\n\tmachine_index = %d\n", head->tag, head->machine_index);
                    received_count++;

                    /* switch case based on head.tag */
                    switch ( head->tag ) {
                        case 0: ; //data_pkt
                            data_pkt *pkt = buf;
                            printf("\n(data_pkt)\n");
                            printf ("from machine #%d\t pkt_index%d\t random_num%d", pkt->head.machine_index, pkt->pkt_index, pkt->rand_num);
                            printf("lio_arr = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]); 
                            
                            /* check if we can store pkt */
                            if ( received_pkts[pkt->pkt_index % WINDOW_SIZE][head->machine_index - 1] != NULL || !check_store(write_arr, lio_arr, pkt) ) {
                                free(pkt);
                                continue;
                            }
                            
                            //special case: when pkt_index = 1, we're receiving a machines 1st pkt (mark in write_arr)
                            if ( pkt->pkt_index == 1 ) 
                                write_arr[head->machine_index - 1] = pkt->counter;

                            //store incoming packet into our grid (received_pkts)
                            received_pkts[pkt->pkt_index % WINDOW_SIZE][head->machine_index - 1] = pkt; 
                            printf("\n\tstoring the pkt into grid at [%d][%d]\n", pkt->pkt_index % WINDOW_SIZE, head->machine_index - 1);

                            print_grid(WINDOW_SIZE, num_machines, received_pkts); 

                            //check if our counter can be updated 
                            if ( pkt->counter > counter ) 
                                counter = pkt->counter;

                            /* check if the pkt is in order */
                            if ( pkt->pkt_index == (n = expected_pkt[head->machine_index - 1]) ) {
                                expected_pkt[head->machine_index - 1]++;
                            } else if ( pkt->pkt_index > n ) { // packets were missed
                                
                                nack_counter[head->machine_index - 1] += (pkt->pkt_index - n);
                                printf("updated nack counter for machine %d to %d nacks", head->machine_index,nack_counter[head->machine_index - 1]) ;
                                expected_pkt[head->machine_index - 1] = pkt->pkt_index + 1;
                                printf("\tupdated expected index:  %d \n", expected_pkt[head->machine_index - 1]) ;

                                if ( nack_counter[head->machine_index - 1] >= NACK_WINDOW - 1 ) {
                                    //send fb pkt
                                    feedback_pkt *fb_ptr;
                                    fb_ptr = send_feedback(&send_addr, num_machines, received_pkts, lio_arr, machine_index, nack_counter, expected_pkt, ss);
                                    free(fb_ptr);
                                }

                                printf("%d packet's are missed\n", nack_counter[head->machine_index - 1]); 
                            
                            } else {
                                //nack was received
                                nack_counter[head->machine_index - 1]--;
                                printf("nacked packet %d is recieved\n", pkt->pkt_index);
                            }

                            printf("lio_arr = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]); 
                    
                            /* check if we can write */
                 
                            if ( pkt->pkt_index == lio_arr[head->machine_index - 1] + 1 ) {
                                write_arr[head->machine_index - 1] = pkt->counter; 
                                //^just got a msg that's +1 from lio -> might have a 0 in write_arr so need to update it

                                //we can start writing when we have atleast 1 msg from every process in order 
                                printf("\n\tChecking if we can write...write_arr = ");
                                for(i = 0; i < num_machines; i++) printf("%d ", write_arr[i]);
                                
                                //Attempting to write to file, if there is a 0 in write_arr, we cant write  
                                int min_machine; 
                                while ( ( min_machine = get_min_index(write_arr, num_machines) ) != -2 && (write_arr[min_machine] != -1) ) { 
                                    printf("\n\twriting ...\n");
                                    int col = min_machine;
                                    int row = (lio_arr[min_machine] + 1) % WINDOW_SIZE; 
                                    data_pkt *write_pkt = received_pkts[row][col];
                                    
                                    if(write_pkt == NULL)printf(" null at 338\n");
                                    //update the lio_arr 
                                    lio_arr[min_machine] = write_pkt->pkt_index;
  
                                    //write it to the file
                                    char buf_write[sizeof(write_pkt->rand_num)];
                                    sprintf(buf_write, "%d", write_pkt->rand_num); //converts int to a str before writing it
                                    fprintf(fw, "%2d, %8d, %8d\n", write_pkt->head.machine_index, write_pkt->pkt_index, write_pkt->rand_num);
                                    fflush(fw); //delete all fflush before submission

                                    printf("lio_arr = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]);
                                    printf("\tacks_received = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                                    //we can set write_pkt to null, only if its not our machines msgs
                                    if ( col + 1 != machine_index ) {
                                        free(write_pkt);
                                        received_pkts[row][col] = NULL;
                                    }

                                    printf("\n");
                                    print_grid(WINDOW_SIZE, num_machines, received_pkts);
                                    
                                    //find the spot of the next minimum
                                    int next_row = (row + 1) % WINDOW_SIZE;
                                    write_pkt = received_pkts[next_row][col];
                                
                                    //in case there is no pkt here ... done writing
                                    if ( write_pkt == NULL ) {
                                        printf("done writing\n");
                                        write_arr[col] = 0; 
                                    } else { //there is another pkt to write
                                        // make sure we haven't looped around to the start of the window in our machine
                                        if (col == machine_index-1 && write_pkt->pkt_index == acks_received[machine_index-1] + WINDOW_SIZE) {
                                                write_arr[col] = 0;
                                                printf("done writing\n");
                                                continue;
                                        }
                                        write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                        if ( write_pkt->counter == -1 ) { 
                                            lio_arr[col] = write_pkt->pkt_index;
                                            free(write_pkt);
                                            received_pkts[row][col] = NULL;
                                            printf("deleting final packet for machine %d", col);
                                        }
                                    }
                                }

                                if ( min_machine != -2 && write_arr[min_machine] == -1 ) {
                                        //possibility of being done
                                        if ( exit_case(acks_received, write_arr, num_machines, num_packets) ) { 
                                            clean_exit(&send_addr, num_machines, lio_arr, machine_index, ss); 
                                            return 0;    
                                        }
                                }
                            }
                            break;
                        case 1: ; //feedback
                            printf("\ncase: feedback_pkt\n");
                            feedback_pkt *fb_pkt; 
                            fb_pkt = (feedback_pkt*)buf;
                            print_fb_pkt(fb_pkt, num_machines);
                            
                            //check fb_pkt's acks -> might be able to delete some of our stored pkts 
                            int new_ack = fb_pkt->acks[machine_index - 1];
                            
                            //in case a machine's ack == our minimum pkt index sent out, they aren't getting our recent pkts
                            //so we send last pkt again to try and restart transfer of data
                            //TODO: check that its not the final pkt -- our final pkt is getting deleted somehow
                            if ( new_ack == acks_received[machine_index-1] && new_ack != num_packets+1) 
                            {
                                //resend our most recent pkt sent
                                data_pkt *temp = received_pkts[pkt_index % WINDOW_SIZE][machine_index-1]; 
                                printf("\ntag = %d, pkt_index = %d\n", temp->head.tag, temp->pkt_index);
                                bytes = sendto( ss, temp, sizeof(data_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                printf("\nre sent our last pkt bc of inactivity\n");
                            }

                            printf("\t\nbefore acks_received = ");
                            for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                            printf("\nnew_ack = %d", new_ack);
                            if ( new_ack > acks_received[fb_pkt->head.machine_index - 1] ) { 
                                acks_received[fb_pkt->head.machine_index - 1] = new_ack;
                                int acks_min = find_min_ack(acks_received, machine_index, num_machines);
                                if ( acks_min == acks_received[machine_index - 1]) { //cant shift
                                    printf("\ncannot shift our window");
                                } else {
                                    printf("\nwe can shift to pkt_index: %d\n", acks_min);
                                    int old_ack = acks_received[machine_index - 1];
                                    acks_received[machine_index - 1] = acks_min; //update the total ack
                                    printf("\n\tour updated acks_received = ");
                                    for(int i=0;i<num_machines;i++) printf(" %d ", acks_received[i]);

                                    /* shifts the window by deleting packets */
                                    for (int i = old_ack+1; i <= acks_min && i <= lio_arr[machine_index-1]; i++) {
                                        int delete_pkt_index = i;
                                         
                                        printf("\ndelete_pkt_index = %d\n", delete_pkt_index % WINDOW_SIZE);
                                        data_pkt *temp = received_pkts[ (delete_pkt_index % WINDOW_SIZE) ][machine_index - 1];
                                        free(temp);
                                        received_pkts[ (delete_pkt_index % WINDOW_SIZE) ][machine_index - 1] = NULL;

                                        if(delete_pkt_index == num_packets+1 && exit_case(acks_received, write_arr, num_machines, num_packets)) {
                                            clean_exit(&send_addr, num_machines, lio_arr, machine_index, ss);
                                            return 0;
                                        }
                                    }


/*
                                    int shift_by = acks_min - old_ack; //number of packets we can shift by & delete 
                                    printf("\tshift_by = %d\n", shift_by);

                                    // changed i back to start at 1 -- technically our old ack shouldn't exist any more
                                    for ( int i = 1; i <= shift_by; i++ ) 
                                    {
                                        int delete_pkt_index = old_ack+i;
                                        // check to make sure we are not deleting a packet that we haven't written yet
                                        if (delete_pkt_index > lio_arr[machine_index - 1]) {
                                                printf("stop deleting packets we haven't written\n");
                                                continue;
                                        }
                                        printf("\ndelete_pkt_index = %d\n", delete_pkt_index % WINDOW_SIZE);
                                        data_pkt *temp = received_pkts[ (delete_pkt_index % WINDOW_SIZE) ][machine_index - 1];
                                        free(temp);
                                        received_pkts[ (delete_pkt_index % WINDOW_SIZE) ][machine_index - 1] = NULL;
                                        printf("\n...deleted pkt in [%d][%d]\n", delete_pkt_index % WINDOW_SIZE, machine_index - 1);
                                    }
                                    //cus i dont think this below means we are done (we need one more avk of the fp)
                                    //if we deleted the FP - update LIO
                                    if (old_ack + shift_by == num_packets + 1) {
                                        lio_arr[machine_index-1] = num_packets + 1;
                                        if ( exit_case(acks_received, write_arr, num_machines, num_packets) )
                                        {
                                            clean_exit(&send_addr, num_machines, lio_arr, machine_index, ss);
                                            return 0;
                                        }
                                    }
*/
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
                                    bytes = sendto( ss, missing_pkt, sizeof(data_pkt), 0,
                                                (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    printf("\nresent nack = %d of size = %d\n", missing_pkt->pkt_index, bytes);
                                }
                            }
                            break;
                        case 2: ; //final_pkt
                            printf("\n>final_pkt\n");
                            data_pkt *fp = buf; 
                            
                            /* check if we can store it yet */
                            if ( received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1] != NULL || !check_store(write_arr, lio_arr, fp) ) {
                                free(fp);
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
                                
                                if (nack_counter[head->machine_index - 1] >= NACK_WINDOW - 1) {
                                    //send fb pkt
                                    feedback_pkt *fb_ptr;
                                    fb_ptr = send_feedback(&send_addr, num_machines, received_pkts, lio_arr, machine_index, nack_counter, expected_pkt, ss);
                                    free(fb_ptr);
                                }
                                printf("%d packet's are missed\n", nack_counter[head->machine_index - 1]);
                            } else {
                                // nack was received
                                nack_counter[head->machine_index - 1]--;
                                printf("nacked packet %d is recieved from machine%d\n", fp->pkt_index, fp->head.machine_index);
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
                                        free(write_pkt);
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
                                        // make sure we haven't looped around to the start of the window in our machine
                                        if (col == machine_index-1 && write_pkt->pkt_index == acks_received[machine_index-1] + WINDOW_SIZE) {
                                                write_arr[col] = 0;
                                                printf("done writing\n");
                                                continue;
                                        }

                                        write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                        if (write_pkt->counter == -1) {
                                            lio_arr[col] = write_pkt->pkt_index;
                                            free(write_pkt);
                                            received_pkts[row][col] = NULL;
                                            printf("deleting final packet for machine %d", col);

                                        }
                                    }
                                }
                                if (min_machine != -2 && write_arr[min_machine] == -1) {
                                        if ( exit_case(acks_received, write_arr, num_machines, num_packets) ) { 
                                            clean_exit(&send_addr, num_machines, lio_arr, machine_index, ss); 
                                            return 0;
                                        }
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
                //send fb pkt
                feedback_pkt *fb_ptr;
                fb_ptr = send_feedback(&send_addr, num_machines, received_pkts, lio_arr, machine_index, nack_counter, expected_pkt, ss);
                free(fb_ptr);
            }

            /*check our recieved_count and recv_threshold to determine if we need to send a burst OR if a certain time passed */
            int timer = 5;
            struct timeval curr_time;
            gettimeofday(&curr_time, NULL);
            printf("\ntime difference in mili sec= %ld\n", (curr_time.tv_usec - t1.tv_usec)/1000);
            if ( (received_count%RECV_THRESHOLD == RECV_THRESHOLD - 1) || ((curr_time.tv_usec - t1.tv_usec)/1000) > timer ) {
                // send a burst
                int burst = (int) WINDOW_SIZE * 0.1;  
                while ( burst > 0 && pkt_index+1 <= num_packets+1 && pkt_index+1 <= acks_received[machine_index - 1] + WINDOW_SIZE ) 
                { 
                    pkt_index++;
            
                    data_pkt *new_pkt;
                    new_pkt = malloc(sizeof(data_pkt));

                        if (pkt_index == num_packets + 1) {
                                new_pkt->head.tag = 2; 
                                new_pkt->counter = -1;
                        } else {
                                new_pkt->head.tag = 0; 
                                counter++;
                                new_pkt->counter = counter;
                        }

                    new_pkt->pkt_index = pkt_index;
                    new_pkt->head.machine_index = machine_index;

                    new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil 
                    //memset(new_pkt->payload, 0, sizeof(int)*350);

                    /*save the new_pkt in received_pkts grid*/
                    received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;

                        //if its the first packet being sent in the window, update write arr
                        if ( write_arr[machine_index - 1] == 0 ) 
                            write_arr[machine_index - 1] = pkt_index; 
                    
                    
                    /* send the packet */
                    bytes = sendto( ss, new_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
                    printf("\nI(machine#%d) sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n\tcounter = %d", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num, new_pkt->counter);
                    burst--;
                }
                //send fb pkt
                feedback_pkt *fb_ptr;
                fb_ptr = send_feedback(&send_addr, num_machines, received_pkts, lio_arr, machine_index, nack_counter, expected_pkt, ss);
                free(fb_ptr);
                
                t1 = curr_time;
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

//we can exit if all ints in write_arr = -1, meaning every msg from every machine is written && acks_received all equal
//our num_pkts + 1 -> meaning every other machine has confirmed they received my final pkt
bool exit_case(int acks_received[], int write_arr[], int num_machines, int num_packets) {
    printf("\nwrite_arr: ");
    for(int i = 0; i < num_machines; i++) printf(" %d ", write_arr[i]);
    printf("\nacks_received: ");
    for(int i = 0; i < num_machines; i++) printf(" %d ", acks_received[i]);
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
    printf("\tacks = ");
    for(int i = 0; i < num_machines; i++) printf(" %d ", fb->acks[i]);
}

/* returns the value of the smallest ack */
int find_min_ack ( int *received_acks, int machine_index, int num_machines) {
    int min = -1;
    for(int i = 0; i < num_machines; i++) {
        //we skip our own index & any -1's
        if ( i != (machine_index - 1) && received_acks[i] != -1 ) {
            //cannot shift bc a pkt hasn't received anything grater than our last ack
            if ( received_acks[i] == received_acks[machine_index - 1] ) return received_acks[machine_index - 1];
            if ( min == -1 ) min = received_acks[i]; //when it's the first valid value, adopt it
            else if ( received_acks[i] < min ) min = received_acks[i];
        }
    }
    return min;
}

void clean_exit(struct sockaddr_in *send_addr, int num_machines, int *lio_arr, int machine_index, int ss) {
    printf("Clean exit\n");
   
    //sends ~a few FB pkts in case another machine is waiting on the last ack that lets them close out ...   
    feedback_pkt *fb;
    fb = malloc(sizeof(feedback_pkt));
    fb->head.tag = 1; 
    fb->head.machine_index = machine_index; 
    
    memcpy(fb->acks, lio_arr, num_machines*sizeof(int));
    
    //since its not missing anything -> just set all to 0
    for(int i = 0; i < num_machines; i++) fb->nacks[0][i] = 0;
    for (int i = 0; i < 10; i++) sendto( ss, fb, sizeof(feedback_pkt), 0, (struct sockaddr *)send_addr, sizeof(*send_addr) );
    free(fb);

    //free all the FP pkts? 
    return;
}

/* creates a feedback packet, multicasts it and returns a pointer to it */
feedback_pkt *send_feedback(struct sockaddr_in *send_addr, int num_machines, data_pkt *received_pkts[WINDOW_SIZE][num_machines], int *lio_arr, int machine_index, int *nack_counter, int *expected_pkt, int ss)
{
    feedback_pkt *fb;
    fb = malloc(sizeof(feedback_pkt));
    fb->head.tag = 1;
    fb->head.machine_index = machine_index;

    printf("\n\t>>send_fb(): passed it\n\tlio_arr = ");
    for(int i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]);
    printf("\n\t---");

    memcpy(fb->acks, lio_arr, ( num_machines * sizeof(int) ) );
    fill_nacks(fb, WINDOW_SIZE, num_machines, received_pkts, nack_counter, lio_arr, expected_pkt);

    printf("\nafter memcpy; fb acks = ");
    for(int i=0;i<num_machines;i++) printf(" %d ",fb->acks[i]);
    printf("after fill-nacks; fb nacks = ");
    for(int i=0;i<num_machines;i++) printf(" %d ",fb->nacks[0][i]);

    int nwritten = sendto( ss, fb, sizeof(feedback_pkt), 0, (struct sockaddr *)send_addr, sizeof(*send_addr) );
    printf("\n\n\tSENT A FB PKT of size = %d\n\n", nwritten);
    print_fb_pkt(fb, num_machines);
    printf("%d packet's are missed\n", nack_counter[machine_index - 1]);
    printf("\nleaving function send_fbpkt \n");
    return fb;
}

