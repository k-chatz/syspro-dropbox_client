OBJS    = main.o options.o list.o buffer.o connection.o client.o file.o handler.o request.o session.o
SOURCE  = main.c options.c list.c buffer.c connection.c client.c file.c handler.c request.c session.c
HEADER  = options.h list.h buffer.h client.h connection.h file.h handler.h request.h session.h
OUT	= dropbox_client
CC  = gcc
FLAGS   = -c -Wall

all: $(OBJS)
	$(CC) $(OBJS) -o $(OUT) -pthread

main.o: main.c
	$(CC) $(FLAGS) main.c

options.o: options.c
	$(CC) $(FLAGS) options.c

list.o: list.c
	$(CC) $(FLAGS) list.c

buffer.o: buffer.c
	$(CC) $(FLAGS) buffer.c

connection.o: connection.c
	$(CC) $(FLAGS) connection.c

client.o: client.c
	$(CC) $(FLAGS) client.c

file.o: file.c
	$(CC) $(FLAGS) file.c

handler.o: handler.c
	$(CC) $(FLAGS) handler.c

request.o: request.c
	$(CC) $(FLAGS) request.c

session.o: session.c
	$(CC) $(FLAGS) session.c

clean:
	rm -f $(OBJS) $(OUT)
