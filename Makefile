#
# file:        Makefile - programming assignment 3
#

CFLAGS = -D_FILE_OFFSET_BITS=64 -g
FILE = homework

# note that implicit make rules work fine for compiling x.c -> x
# (e.g. for mktest). Also, the first target defined in the file gets
# compiled if you run without an argument.
#
all: homework mktest

# '$^' expands to all the dependencies (i.e. misc.o homework.o image.o)
# and $@ expands to 'homework' (i.e. the target)
#
homework: misc.o $(FILE).o image.o
	gcc -g $^ -o $@ -lfuse

clean: 
	rm -f *.o $(FILE) mktest

