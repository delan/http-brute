CFLAGS=-std=c99
LDLIBS=-lpthread
bruteforce: bruteforce.o base64.o
clean:
	rm -f bruteforce *.o
