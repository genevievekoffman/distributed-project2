#include "net_include.h"

int main()
{
    //struct ip_mreq     mreq;
    unsigned char      ttl_val;

    struct sockaddr_in send_addr;
    struct sockaddr_in name;
    struct ip_mreq     mreq;
    int ss, sr;
    int mcast_addr;
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
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );
    
    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq,
        sizeof(mreq)) < 0)
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if ( ss < 0 ) {
        perror("Mcast: socket");
        exit(1);
    }
    
    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val,sizeof(ttl_val)) < 0) {
        printf("Mcast: problem in setsockopt of multicast ttl %d \n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    char *buf = "start";
    int bytes = sendto( ss, buf, strlen(buf), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
    if ( bytes == 0 ) printf("error");
    return 0;
}
