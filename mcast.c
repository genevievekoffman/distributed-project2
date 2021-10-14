#include "net_include.h"
#include "packets.h"
#include <time.h>
#include <stdbool.h>


#define MAX_MACHINES 10
#define WINDOW_SIZE 20
#define RECV_THRESHOLD 10

bool check_write(int arr[], int sz);
int get_min_index(int arr[], int sz);
void print_grid(int rows, int cols, data_pkt *grid[rows][cols]); 
bool check_store(int lio_arr[], data_pkt *pkt);
//void check_acks(data_pkt *pkt, int *acks_received, int machine_index);
int get_min_ack_received(int acks_received[], int machine_index, int num_machines);
bool only_min(int acks_received[], int machine_index, int num_machines);
bool exit_case(int acks_received[], int write_arr[], int num_machines);
void fill_nacks(feedback_pkt *fb, int rows, int cols, data_pkt *grid[rows][cols], int *nack_counter);
void print_fb_pkt(feedback_pkt *fb, int num_machines);
int find_min_ack ( int *received_acks, int machine_index, int num_machines);

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
    
            //create header & a data_pkt 
            header data_head;
            data_head.tag = 0;
            data_head.machine_index = machine_index;
            data_pkt *new_pkt;
           
            new_pkt = malloc(sizeof(data_pkt));
            new_pkt->head = data_head; 
            new_pkt->pkt_index = pkt_index;
            new_pkt->counter = counter;

            new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil 
            //memcpy(new_pkt->acks, lio_arr, sizeof(lio_arr));

            //set new_pkt->payloads = random 1400 bytes?
            
            //save the new_pkt in received_pkts grid
            received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;
            //printf("\nstoring our packet(#%d) at [%d][%d]\n", pkt_index, pkt_index % WINDOW_SIZE, machine_index - 1);
            
            //if its the first ever packet being sent, set write_arr = 1 at its spot(**unless its final pkt ...)
            if ( pkt_index == 1 ) {
                write_arr[machine_index - 1] = 1; 
            }
            
            /* send the packet */
            char buffer[sizeof(*new_pkt)]; //save the new data pkt into buffer before sending it
            memcpy(buffer, new_pkt, sizeof(*new_pkt)); //copies sizeof(new_pkt) bytes into buffer from new_pkt
            bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
            printf("\nI(machine#%d) sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n\tcounter = %d", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num, new_pkt->counter);
            //printf("\n\tacks = ");
            //for(i=0;i<num_machines;i++) printf(" %d ", new_pkt->acks[i]);
            burst--;
        }

        //send final packet
        if ( pkt_index == num_packets ) {
            pkt_index++;
            final_pkt *final_msg;
            final_msg = malloc(sizeof(final_msg));
            header head;
            head.tag = 2;
            head.machine_index = machine_index;
            final_msg->head = head;
            final_msg->pkt_index = pkt_index;
            final_msg->counter = -1;
            char buffer[sizeof(*final_msg)]; //save the final pkt into buffer before sending it
            memcpy(buffer, final_msg, sizeof(*final_msg));
            bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
            printf("\nSent final pkt: bytes = %d\n", bytes);
            received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = (data_pkt*)final_msg;
        }

        //listen for incoming packets or timeout
        
        //how long do we want to be recieving pkts for? until we dont receive one in x amnt of time? possibly receiving in bursts too
        
        print_grid(WINDOW_SIZE, num_machines, received_pkts);

        for(;;)
        { 
            //case that we received xx amount of pkts --> send a fb pkt (later on, add the case we r missing x pkts)
            printf("\nwaiting for an event? ..\n");
            

            //in the case we've received RECV_THRESHOLD pkts --> send a FB pkt
            if ( received_count % RECV_THRESHOLD == RECV_THRESHOLD - 1 ) { //every RECV_THRESHOLD pkts received (did -1 so it wouldn't send when = 0)
                printf("\n\tI should send a FB pkt here\n");
                //send_fb();
            }

            read_mask = mask;
            timeout.tv_sec = 1; 
            timeout.tv_usec = 0;
            num = select( FD_SETSIZE, &read_mask, NULL, NULL, &timeout); //event triggered
            if ( num > 0 ) { 
                if ( FD_ISSET( sr, &read_mask) ) { //recieved some type of packet  
                    //received_count++; //does this increment also when we receive our OWN pkts (bc we ignore that in line 248)

                    char buf[sizeof(data_pkt)];
                    header *head;
                    bytes = recv( sr, buf, sizeof(buf), 0); 
                    head = (header*)buf;
                    
                    /* in the case that a machine received a packet from itself, ignore it*/ 
                    if ( head->machine_index == machine_index ) continue; 

                    printf("received header:\n\ttag = %d\n\tmachine_index = %d\n", head->tag, head->machine_index);
                    received_count++;
                    /* switch case based on head.tag */
                    
                    switch ( head->tag ) {
                        case 0: ; //data_pkt
                            data_pkt *pkt;
                            pkt = malloc(sizeof(data_pkt));
                            memcpy(pkt, buf, sizeof(data_pkt));
                            
                            //pkt = (data_pkt*)buf;
                            printf("\n(data_pkt)\n");
                            
                            /* check if we can store pkt */
                            if ( !check_store(lio_arr, pkt) ) {
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

                            
                            //I think this isn't working because we are not updating our expected_pkt ...
                            if (pkt->pkt_index == (n = expected_pkt[head->machine_index - 1])) {
                                // packet is what we excpect
                                //printf("packet index %d is in order\n", n);
                                expected_pkt[head->machine_index - 1]++;
                            } else if(pkt->pkt_index > n) { 
                                // packets were missed
                                
                                // increase 
                                nack_counter[head->machine_index - 1] += (pkt->pkt_index - n);
                                expected_pkt[head->machine_index - 1] = n+1;

                                printf("%d packet's are missed\n", nack_counter[head->machine_index - 1]); //WHY IS THIS PRINTING SUCH HUGE #S?
                            } else {
                                // nack was received
                                nack_counter[head->machine_index - 1]--;
                                printf("nacked packet %d is recieved\n", pkt->pkt_index);
                            }

                            
                            /* check the incoming pkt's acks & update our acks_received */
                            //check_acks(pkt, acks_received, machine_index);
                            
                            if (only_min(acks_received, machine_index, num_machines)) {
                                //get min
                                acks_received[machine_index - 1] = get_min_ack_received(acks_received, machine_index, num_machines);
                                //shift to min^
                                printf("SHIFT to %d", acks_received[machine_index - 1] );

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
                                int min_machine = get_min_index( write_arr, num_machines );
                                while ( min_machine != -2 ) { 
                                    printf("\n\twriting ...\n");
                                   
                                    int col = min_machine;
                                    int row = (lio_arr[min_machine] + 1) % WINDOW_SIZE; //since its the last written we need to + 1
                                    data_pkt *write_pkt = received_pkts[row][col];
                                    //printf("\ngoing to write pkt at index [%d][%d] to file\n", row, col);
                                                                       
                                    if (write_pkt->counter == -1) { 
                                        if ( exit_case(acks_received, write_arr, num_machines) ) return 0;
                                        printf("\n370: couldve been done .... missing smtng\n");
                                    }

                                    //printf("\n...that packet contains: rand_num = %d & counter = %d\n", write_pkt->rand_num, write_pkt->counter);
                                    
                                    /* write it to the file */
                                    char buf_write[sizeof(write_pkt->rand_num)];
                                    sprintf(buf_write, "%d", write_pkt->rand_num); //converts int to a str before writing it
                                    fprintf(fw, "%2d, %8d, %8d\n", write_pkt->head.machine_index, write_pkt->pkt_index, write_pkt->rand_num);
                                    fflush(fw);

                                    //update the lio_arr 
                                    lio_arr[min_machine] = write_pkt->pkt_index;
                                    printf("lio_arr = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]);
                                    printf("\tacks_received = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                                    //we can set write_pkt to null, only if its not our machines msgs
                                    if ( col + 1 != machine_index ) {
                                        //printf("\nwe can set write_pkt to null\n");
                                        //we can delete the packet and set it = null & check out the next address
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
                                        min_machine = get_min_index( write_arr, num_machines );
                                    } else { //there is another pkt to write
                                        write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                        min_machine = get_min_index( write_arr, num_machines );
                                    }
                                        
                                }

                            }

                            
                            /* check if we can send a FB pkt right after we have just handled a data pkt */
                            if ( received_count % RECV_THRESHOLD == RECV_THRESHOLD - 1) { //every RECV_THRESHOLD pkts --> we can always change how to detmerine this
                                printf("\n\n\treceived_count = %d ... so we will send a FB pkt\n\n", received_count); 
                                //do i need to allocate mem every time we creat one and then just free it after i send it?
                                header fb_head;
                                fb_head.tag = 1; //FB_PKT;
                                fb_head.machine_index = machine_index;
                                feedback_pkt fb;
                                fb.head = fb_head;
                                memcpy(fb.acks, lio_arr, sizeof(lio_arr));
                                //fill fb.nacks based on nack_counter
                                fill_nacks(&fb, WINDOW_SIZE, num_machines, received_pkts, nack_counter); 
                                
                                char buffer[sizeof(feedback_pkt)];
                                memcpy(buffer, &fb, sizeof(fb));
                                nwritten = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                printf("\n\nSENT A FB PKT of size = %d\n\n", nwritten);
                                print_fb_pkt(&fb, num_machines);
                            }

                            break;
                        case 1: ; //feedback
                            printf("\ncase: feedback_pkt\n");
                            feedback_pkt *fb_pkt;
                            fb_pkt = (feedback_pkt*)buf;
                            print_fb_pkt(fb_pkt, num_machines);
                            
                            //check the feedback pkts acks -> might be able to delete some of our pkts 
                            
                            int new_ack = fb_pkt->acks[machine_index - 1];
                            printf("\nnew_ack = %d", new_ack);
                            if ( new_ack > acks_received[fb_pkt->head.machine_index - 1] ) {
                                acks_received[fb_pkt->head.machine_index - 1] = new_ack;
                                int acks_min = find_min_ack(acks_received, machine_index, num_machines);
                                //int get_min_ack = get_min_ack_received(acks_received, machine_index, num_machines);
                                printf("\nget_min_ack = %d", acks_min);
                                if (acks_min == acks_received[machine_index - 1] ) { //cant shift
                                    printf("\ncannot shift our window");
                                } else {
                                    printf("\nwe can shift to %d\n", acks_min);
                                    acks_received[machine_index - 1] = acks_min; //update the total ack
                                    printf("\n\tour updated acks_received = ");
                                    for(int i=0;i<num_machines;i++) printf(" %d ", acks_received[i]);
                                    //TODO: shift window by deleting the pkts before that pkt_index(acks_min) and sending more pkts
                                }
                            }


                            int num_nacks = fb_pkt->nacks[0][machine_index-1]; 
                            printf("\nnum_nacks = %d\n", num_nacks); 
                            if (num_nacks > 0) {
                                printf("\nresending nacks\n");
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
                            break;
                        case 2: //final_pkt
                            printf("\n(final_pkt)\n");
                            final_pkt *fp;
                            fp = malloc(sizeof(final_pkt));
                            memcpy(fp, buf, sizeof(final_pkt));
                            
                            /* check if we can store it yet */
                            if ( !check_store(lio_arr, (data_pkt*)fp) ) {
                                break;
                            }

                            received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1] = (data_pkt*)fp;
                            printf("\n\t%d < %d < %d ... so we can store it!\n", n, fp->pkt_index, n + WINDOW_SIZE);
                            
                            /* check if we can write */
                 
                            pkt = received_pkts[fp->pkt_index % WINDOW_SIZE][head->machine_index - 1]; 
                            if (pkt ->pkt_index == lio_arr[head->machine_index - 1] + 1) {
                                //if we just got the msg that's +1 from lio this means we might have a 0 in write_arr, need to update it
                                write_arr[head->machine_index - 1] = pkt->counter;

                                //we can start writing when we have atleast 1 msg from every proces in top row 
                                printf("\n\tChecking if we can write...write_arr = ");
                                for(i = 0; i < num_machines; i++) printf("%d ", write_arr[i]);
                                
                                //Attempting to write to file, if there is a 0 in write_arr, we cant write  
                                int min_machine = get_min_index( write_arr, num_machines );

                                while ( min_machine != -2 ) { 
                                    printf("\n\twriting ...\n");
                                   
                                    int col = min_machine;
                                    int row = (lio_arr[min_machine] + 1) % WINDOW_SIZE; //since its the last written we need to + 1
                                    data_pkt *write_pkt = received_pkts[row][col];
                                    //printf("\ngoing to write pkt at index [%d][%d] to file\n", row, col);
                                                                       
                                    if (write_pkt->counter == -1) {
                                        //possibliity of being done
                                        if ( exit_case(acks_received, write_arr, num_machines) ) return 0;
                                        printf("\n...mightve been done but smtng is not finished\n");
                                    }

                                    //printf("\n...that packet contains: rand_num = %d & counter = %d\n", write_pkt->rand_num, write_pkt->counter);
                                    
                                    /* write it to the file */
                                    char buf_write[sizeof(write_pkt->rand_num)];
                                    sprintf(buf_write, "%d", write_pkt->rand_num); //converts int to a str before writing it
                                    fprintf(fw, "%2d, %8d, %8d\n", write_pkt->head.machine_index, write_pkt->pkt_index, write_pkt->rand_num);
                                    fflush(fw);

                                    //update the lio_arr 
                                    lio_arr[min_machine] = write_pkt->pkt_index;
                                    printf("lio_arr = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",lio_arr[i]);
                                    printf("\tacks_received = ");
                                    for(i=0;i<num_machines;i++) printf(" %d ",acks_received[i]);
                                    //we can set write_pkt to null, only if its not our machines msgs
                                    if ( col + 1 != machine_index ) {
                                        //printf("\nwe can set write_pkt to null\n");
                                        //we can delete the packet and set it = null & check out the next address
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
                                        min_machine = get_min_index( write_arr, num_machines );
                                    } else { //there is another pkt to write
                                        write_arr[col] = write_pkt->counter; //update the write_arr to hold its counter 
                                        min_machine = get_min_index( write_arr, num_machines );
                                    }
                                        
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
                /* how do we want to do this: send a nack or resend our msgs? */
            }

            // check our recieved_count and recv_threshold to determine if we need to send a burst
            if (received_count%RECV_THRESHOLD == 0) {
                // send a burst
                int burst = 2; //make sure burst < window size  
                while ( burst > 0 && pkt_index < num_packets && pkt_index < acks_received[machine_index - 1] + WINDOW_SIZE ) { 
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

                    new_pkt->rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil 
                    //memcpy(new_pkt->acks, lio_arr, sizeof(lio_arr));

                    //set new_pkt->payloads = random 1400 bytes?
                    
                    /*save the new_pkt in received_pkts grid*/
                    received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = new_pkt;
                    //printf("\nstoring our packet(#%d) at [%d][%d]\n", pkt_index, pkt_index % WINDOW_SIZE, machine_index - 1);
                    
                    //if its the first ever packet being sent, set write_arr = 1 at its spot(**unless its final pkt ...)
                    if ( pkt_index == 1 ) {
                        write_arr[machine_index - 1] = 1; 
                    }
                    
                    /* send the packet */
                    char buffer[sizeof(*new_pkt)]; //save the new data pkt into buffer before sending it
                    memcpy(buffer, new_pkt, sizeof(*new_pkt)); //copies sizeof(new_pkt) bytes into buffer from new_pkt
                    bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
                    printf("\nI(machine#%d) sent: \n\thead: tag = %d, machine_index = %d\n\tpkt_index = %d\n\trand_num = %d\n\tcounter = %d", machine_index, new_pkt->head.tag, new_pkt->head.machine_index, pkt_index, new_pkt->rand_num, new_pkt->counter);
                    //printf("\n\tacks = ");
                    //for(i=0;i<num_machines;i++) printf(" %d ", new_pkt->acks[i]);
                    burst--;
                }

                //send final packet
                if ( pkt_index == num_packets ) {
                    pkt_index++;
                    final_pkt *final_msg;
                    final_msg = malloc(sizeof(final_msg));
                    header head;
                    head.tag = 2;
                    head.machine_index = machine_index;
                    final_msg->head = head;
                    final_msg->pkt_index = pkt_index;
                    final_msg->counter = -1;
                    char buffer[sizeof(*final_msg)]; //save the final pkt into buffer before sending it
                    memcpy(buffer, final_msg, sizeof(*final_msg));
                    bytes = sendto( ss, buffer, sizeof(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    printf("\nSent final pkt: bytes = %d\n", bytes);
                    received_pkts[pkt_index % WINDOW_SIZE][machine_index - 1] = (data_pkt*)final_msg;
                }

            }
        } 
        //send feedback if nack_counter > 0
    return 0;
}

/* returns the index holding the minimum value (if there are 2+ with same value, the index most left is returned */
int get_min_index(int arr[], int sz) {
    int min_index = 0; 
    int i;
    int neg_count = 0;
    
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

bool check_store(int lio_arr[], data_pkt *pkt) {
    int n;
    if ( (n = lio_arr[pkt->head.machine_index]) < pkt->pkt_index && pkt->pkt_index < n + WINDOW_SIZE ) { 
        //printf("\n\t%d < %d < %d -> so we can store it!\n", n, pkt->pkt_index, n + WINDOW_SIZE);
        return true;
    }
    return false;
}

/* Check acks of an incoming pkt, we might be able to update acks in received_acks */
/*
void check_acks ( data_pkt *pkt, int *acks_received, int machine_index ) {
    int new_ack = pkt->acks[machine_index];
    printf("\n\tpkt->acks[machine_index] = %d\n", pkt->acks[machine_index]);
    //adopt the new ack if its greater than the old one
    if ( new_ack > acks_received[machine_index] ) {
        printf("\n\tadopting ack: new_ack = %d > %d (old ack)\n", new_ack, acks_received[machine_index]);
        acks_received[machine_index] = new_ack;
    }
}
*/

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
bool exit_case(int acks_received[], int write_arr[], int num_machines) {
    for (int i = 0; i < num_machines; i++) {
        if ( acks_received[i] != -1 || write_arr[i] != -1 ) 
            return false; //something is not finished -> cannot exit
    }
    return true;
}

void fill_nacks(feedback_pkt *fb, int rows, int cols, data_pkt *grid[rows][cols], int *nack_counter) { 
    //fill in fb->nacks based off nack_counter 
    //make sure we fill it so that when we exit this method, it is still updated in the main method
    //num_machines is cols!
    printf("\nFILLING FB->NACKS\n");
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

























