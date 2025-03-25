
init_server:server
init_client:client

SERVER_ADDR ?=127.0.0.1
PORT?=8080

client:client_v1.o
	gcc client_v1.o -o client 

server:ML1_server.o
	gcc ML1_server.o -o server

all: ML1_server.o client_v1.o
	gcc ML1_server.o -o server
	gcc client_v1.o -o client

ML1_server.o: ML1_server.c
	gcc -c ML1_server.c

client_v1.o: client_v1.c
	gcc -c client_v1.c

clean:
	rm -f all ML1_server.o client_v1.o client server

echos:
	@./server $(PORT)
echo:
	@./client $(SERVER_ADDR) $(PORT)