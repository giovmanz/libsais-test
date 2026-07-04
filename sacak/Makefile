SHELL=/bin/sh

CC=gcc

#these are for testing
# CFLAGS = -g -O3 -Wall -Winline -std=c11 -pthread

#these are for maximum speed
CFLAGS = -g -O3 -march=native -Wall -Winline -std=c11 -pthread -fomit-frame-pointer -DNDEBUG

# these are for profiling
# CFLAGS = -g -pg -O2 -Wall -Winline  -std=c11 -pthread -fno-omit-frame-pointer -DNDEBUG

# these are for debugging
# CFLAGS = -g -ggdb -Og -Wall -Winline -std=c11 -pthread -fno-omit-frame-pointer



EXECS = sacak

.PHONY: all
all : $(EXECS)


# sacak 8 bit/ 32 bit
sacak: sacak.c sacak-lcp.o 
	$(CC) $(CFLAGS) -o $@ $^

sais64: sais.c build/libsais.a 
	$(CC) $(CFLAGS) -o $@ $^ -DUSE_INT64

# pattern rule for all objects files
%.o: %.c *.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *.a $(EXECS)




