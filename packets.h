#include <stdio.h>
#include <stdlib.h>

#define BYTES 1400
#define MAX_MACHINES 10
#define NACK_WINDOW 5

#define DATA_PKT 0
#define NACK_PKT 1
#define FINAL_PKT 2

typedef struct dummy_header {
    int tag; //0 = data pkt, 1 = nack, 2 = final
    int machine_index;
}header;

typedef struct dummy_data {
    header head;
    int pkt_index;
    int counter;
    int acks[MAX_MACHINES];
    int rand_num;
    int payload[BYTES];
}data_pkt;

typedef struct dummy_nack{
    header head;
    int nacks[MAX_MACHINES][NACK_WINDOW];
}nack_pkt;

typedef struct dummy_final{
    header head;
    int pkt_index;
}final_pkt;
