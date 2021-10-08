#include "net_include.h"
#include "packets.h"
#include <time.h>

#define MAX_MACHINES 10

int main(int argc, char **argv)
{
    struct sockaddr_in name;
    struct sockaddr_in send_addr;

    int                mcast_addr;

    struct ip_mreq     mreq;
    unsigned char      ttl_val;

    int                ss,sr;
    fd_set             mask;
    fd_set             read_mask, write_mask, excep_mask;
    int                bytes;
    int                num;
    char               mess_buf[MAX_MESS_LEN];
    char               input_buf[80];

    int                num_packets;
    int                machine_index;
    int                num_machines;
    int                loss_rate;

    int                counter = 0;
    int                pkt_index = 0; 
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

    /*create last_in_order array of size num_machines*/
    int last_in_order_arr[num_machines];

    mcast_addr = 225 << 24 | 0 << 16 | 1 << 8 | 1; /* (225.0.1.1) */

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
    FD_ZERO( &write_mask );
    FD_ZERO( &excep_mask );
    FD_SET( sr, &mask );
    FD_SET( (long)0, &mask );    /* stdin */
    
    //cannot proceed until start_mcast is called
    
    /*TRANSFER*/

    //sending a packet send_packet():
    int burst = 15;
    while ( burst > 0 && pkt_index < num_packets ) {
        pkt_index++;
        counter++;
        //create header & a data_pkt 
        header data_head;
        data_head.tag = 0;
        data_head.machine_index = machine_index;
        data_pkt new_pkt;
        new_pkt.head = data_head;
        new_pkt.pkt_index = pkt_index;
        new_pkt.counter = counter;
        /*init random num generator*/
        srand(time(NULL));
        new_pkt.rand_num = rand() % 1000000 + 1; //generates random number 1 to 1 mil
        //new_pkt.acks = last_in_order_arr;  //do we need to memcpy the array everytime?
        memcpy(new_pkt.acks, last_in_order_arr, sizeof(last_in_order_arr));

        //set new_pkt->payloads = random 1400 bytes?
    
        /*save the new_pkt in received_pkts grid*/

        /*multicast, send the packet*/
        char buffer[sizeof(new_pkt)]; //save the new data pkt into buffer before sending it
        memcpy(buffer, &new_pkt, sizeof(new_pkt)); //copies sizeof(new_pkt) bytes into buffer from new_pkt
        sendto( ss, buffer, strlen(buffer), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
        printf("Sent: \n\tpkt_index = %d,\n\trand_num = %d\n", pkt_index, new_pkt.rand_num);
        burst--;
    }   

    /*listen for incoming packets*/

    /*receive packets: */
    /*switch case based on tag*/
    
    /*CAST THE PACKET BASED ON THE TAG*/

    //header received_pkt;
    //receiving






    for(;;)
    {
        read_mask = mask;
        num = select( FD_SETSIZE, &read_mask, &write_mask, &excep_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET( sr, &read_mask) ) {
                bytes = recv( sr, mess_buf, sizeof(mess_buf), 0 );
                mess_buf[bytes] = 0;
                printf( "received : %s\n", mess_buf );
            }else if( FD_ISSET(0, &read_mask) ) {
                bytes = read( 0, input_buf, sizeof(input_buf) );
                input_buf[bytes] = 0;
                printf( "there is an input: %s\n", input_buf );
                sendto( ss, input_buf, strlen(input_buf), 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
            }
        }
    }

    return 0;
}
