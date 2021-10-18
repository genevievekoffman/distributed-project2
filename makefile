CC=gcc

CFLAGS = -g -c -Wall -pedantic
#CFLAGS = -ansi -c -Wall -pedantic -D_GNU_SOURCE

all: bcast mcast start_mcast

bcast: bcast.o
	$(CC) -o bcast bcast.o 

mcast: mcast.o recv_dbg.o
	$(CC) -o mcast mcast.o recv_dbg.o

start_mcast: start_mcast.o
	$(CC) -o start_mcast start_mcast.o

clean:
	rm *.o
	rm bcast
	rm mcast
	rm start_mcast
