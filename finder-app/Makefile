.PHONY: clean
all: writer

# cross-compile definition
#CROSS_COMPILE := aarch64-none-linux-gnu-
CROSS_COMPILE :=

CC := gcc
CFLAGS := -Wall

# Automatic Variables https://makefiletutorial.com/#automatic-variables-and-wildcards
# Conditinals https://makefiletutorial.com/#conditional-part-of-makefiles
writer: writer.c
ifdef CROSS_COMPILE
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@
endif
ifndef CROSS_COMPILE
	$(CC) $(CFLAGS) $^ -o $@
endif

clean: 
	rm -f *.o writer