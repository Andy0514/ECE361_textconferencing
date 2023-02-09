all: server.o client.o
	gcc -g server.o -o server -pthread
	gcc -g client.o -o client -pthread

server.o: server.c server.h packet.h
	gcc -c -g server.c -o server.o -pthread

client.o: client.c client.h packet.h
	gcc -c -g client.c -o client.o -pthread

clean:
	rm -f *.o
