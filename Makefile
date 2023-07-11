
CC = gcc       # compiler
CFLAGS = -g    # compilation flags
# LDFLAGS = -g       # debugging symbols in build
LIBS = -lz  -pthread -lcurl     # link with libz

# For students
SRCS = ./starter/png_util/zutil.c ./starter/png_util/crc.c ./paster.c
OBJS = $(SRCS:.c=.o)
CAT_OUTPUT = paster

all: paster

paster: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.d *.o *.out paster ${OBJS} 

