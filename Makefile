all: server.o client.o
	gcc -g server.o -o server
	gcc -g client.o -o client

server.o: server.c server.h packet.h
	gcc -c -g server.c -o server.o -lpthread

client.o: client.c client.h packet.h
	gcc -c -g client.c -o client.o -lpthread

clean:
	rm -f *.o
