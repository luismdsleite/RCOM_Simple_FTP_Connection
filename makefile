# Compiler and linker
CC = gcc

# Flags
CFLAGS = -Wall -g

compile: 
	@gcc ${CFLAGS} -o download download.c -lm

clean:	
	@rm -f download 