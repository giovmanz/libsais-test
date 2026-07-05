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



EXECS = sacak sacak64

.PHONY: all
all : $(EXECS)


# sacak 8 bit/ 32 bit
sacak: sacak.c sacak-lcp.o 
	$(CC) $(CFLAGS) -o $@ $^

sacak64: sacak.c sacak-lcp64.o  
	$(CC) $(CFLAGS) -o $@ $^ -DM64

sacak-lcp64.o: sacak-lcp.c sacak-lcp.h
	$(CC) -c $(CFLAGS) $< -o $@ -DM64

# pattern rule for all objects files
%.o: %.c *.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *.a $(EXECS)




