
init_server:server
init_client:client

SERVER_ADDR ?=127.0.0.1
PORT?=8080
USERNAME?=ANYTHING
NO_OF_CLIENTS?=3

client:client.o
	gcc client.o -o client 

server:server.o
	gcc server.o -o server

all:server.o client.o
	gcc server.o -o server
	gcc client.o -o client

server.o: server.c
	gcc -c server.c

client.o: client.c
	gcc -c client.c
 
clean:
	rm -f all server.o client.o client server

echos:
	@./server $(SERVER_ADDR) $(PORT) $(NO_OF_CLIENTS)
echo:
	@./client $(USERNAME) $(SERVER_ADDR) $(PORT)