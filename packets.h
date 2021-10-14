#include <stdio.h>
#include <stdlib.h>

#define BYTES 1400
#define MAX_MACHINES 10
#define NACK_WINDOW 5

#define DATA_PKT 0
#define FB_PKT 1
#define FINAL_PKT 2

typedef struct dummy_header {
    int tag; //0 = data pkt, 1 = fb_pkt, 2 = final
    int machine_index;
}header;

typedef struct dummy_data {
    header head;
    int pkt_index;
    int counter;
    //int acks[MAX_MACHINES];
    int rand_num;
    int payload[BYTES];
}data_pkt;

typedef struct dummy_feedback{
    header head;
    int nacks[MAX_MACHINES][NACK_WINDOW];
    int acks[MAX_MACHINES];
}feedback_pkt;

typedef struct dummy_final{
    header head;
    int pkt_index;
    int counter; 
}final_pkt;
