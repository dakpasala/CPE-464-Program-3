# CC = gcc
# CFLAGS = -Wall -Wextra -Werror -std=c99 -D_POSIX_C_SOURCE=200809L -g

# # Detect OS and set OpenSSL paths
# UNAME_S := $(shell uname -s)
# ifeq ($(UNAME_S),Darwin)
#     # macOS - use Homebrew OpenSSL
#     OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null || echo /usr/local/opt/openssl)
#     CFLAGS += -I$(OPENSSL_PREFIX)/include
#     LDFLAGS += -L$(OPENSSL_PREFIX)/lib
# endif

# LDFLAGS += -lssl -lcrypto

# COMMON_OBJS = common.o
# SR_OBJS = selective_repeat.o
# LOSSY_OBJS = lossy_udp.o

# all: server client

# server: server.o $(COMMON_OBJS)
# 	$(CC) -o $@ $^ $(LDFLAGS)

# client: client.o $(COMMON_OBJS) $(SR_OBJS) $(LOSSY_OBJS)
# 	$(CC) -o $@ $^ $(LDFLAGS)

# %.o: %.c
# 	$(CC) $(CFLAGS) -c $<

# clean:
# 	rm -f *.o server client

# .PHONY: all clean


CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -D_POSIX_C_SOURCE=200809L -g

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null || echo /usr/local/opt/openssl)
    CFLAGS  += -I$(OPENSSL_PREFIX)/include
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib
endif

LDFLAGS += -lssl -lcrypto

COMMON_OBJS = common.o share_file_logic.o
SR_OBJS     = selective_repeat.o
LOSSY_OBJS  = lossy_udp.o

all: server client

server: server.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o server server.o $(COMMON_OBJS) $(LDFLAGS)

client: client.o $(COMMON_OBJS) $(SR_OBJS) $(LOSSY_OBJS)
	$(CC) $(CFLAGS) -o client client.o $(COMMON_OBJS) $(SR_OBJS) $(LOSSY_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o server client

.PHONY: all clean

