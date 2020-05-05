CC=gcc
CFLAGS=-Wall -Wextra -lm -g -std=c11
DEPS=utils.h cache.h sim.h 
OBJ= utils.o cache.o sim.o main.o
EXE=sgxc

all: main 

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)  

main: $(OBJ) 
	$(CC) $(OBJ) $(CFLAGS) -o $(EXE)

clean:
	rm *.o $(EXE)
