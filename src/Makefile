#############################################################################################
# Makefile
#############################################################################################
# G++ is part of GCC (GNU compiler collection) and is a compiler best suited for C++
CC=g++

# Compiler Flags: https://linux.die.net/man/1/g++
#############################################################################################
# -g: produces debugging information (for gdb)
# -Wall: enables all the warnings
# -Wextra: further warnings
# -O: Optimizer turned on
# -std: c++ 17 is the latest stable version c++2a is the upcoming version
# -I: Add the directory dir to the list of directories to be searched for header files.
# -c: says not to run the linker
# -pthread: Add support for multithreading using the POSIX threads library. This option sets 
#           flags for both the preprocessor and linker. It does not affect the thread safety 
#           of object code produced by the compiler or that of libraries supplied with it. 
#           These are HP-UX specific flags.
#############################################################################################
CFLAGS=-g -Wall -Wextra -O -std=c++17 -pthread
LIBS=-lldap -llber


rebuild: clean all
all: ./bin/twmailer-server ./bin/twmailer-client

clean:
	clear
	rm -f bin/* obj/*

./obj/myclient.o: myclient.c mypw.h
	${CC} ${CFLAGS} -o obj/myclient.o myclient.c -c ${LIBS}

./obj/myserver.o: myserver.c
	${CC} ${CFLAGS} -o obj/myserver.o myserver.c -c

./obj/mypw.o: mypw.c
	${CC} ${CFLAGS} -o obj/mypw.o mypw.c -c

./bin/twmailer-server: ./obj/myserver.o 
	${CC} ${CFLAGS} -o bin/twmailer-server obj/myserver.o ${LIBS}

./bin/twmailer-client: ./obj/myclient.o ./obj/mypw.o
	${CC} ${CFLAGS} -o bin/twmailer-client obj/mypw.o obj/myclient.o 