.DEFAULT_GOAL := all
.PHONY: clean all
all: aesdsocket

# cross-compile definition
#CROSS_COMPILE := aarch64-none-linux-gnu-
CROSS_COMPILE :=

CC := gcc
CFLAGS := -Wall -Werror

# Automatic Variables https://makefiletutorial.com/#automatic-variables-and-wildcards
# Conditinals https://makefiletutorial.com/#conditional-part-of-makefiles
aesdsocket: server.c
ifdef CROSS_COMPILE
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@
endif
ifndef CROSS_COMPILE
	$(CC) $(CFLAGS) $^ -o $@
endif

clean: 
	rm -f *.o aesdsocket