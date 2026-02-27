.DEFAULT_GOAL := all
.PHONY: clean all
all: aesdsocket

# cross-compile definition
#CROSS_COMPILE := aarch64-none-linux-gnu-
CROSS_COMPILE :=

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

# Automatic Variables https://makefiletutorial.com/#automatic-variables-and-wildcards
# Conditinals https://makefiletutorial.com/#conditional-part-of-makefiles
aesdsocket: server.c
	$(CC) $(CFLAGS) $^ -o $@ $(INCLUDES) $(LDFLAGS)


clean: 
	rm -f *.o aesdsocket