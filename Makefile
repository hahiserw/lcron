#Makefile for lcron

PROGRAM=lcrond
CONFIG_FILE=lcron.conf

CONFIG_DIR=/etc
INTSTALL_DIR=/usr/bin

C=gcc
CFLAGS=


all: $(PROGRAM)

lcrond: main.c
	$(C) $(CFLAGS) $< -o $@

# %.o: %.cpp
# 	$(CXX) -c $< -o $@

clean:
	rm $(PROGRAM)

install:
	cp $(PROGRAM) $(INTSTALL_DIR)
	cp $(CONFIG_FILE) $(CONFIG_DIR)

remove:
	rm $(INTSTALL_DIR)/$(PROGRAM)
	rm $(CONFIG_DIR)/$(CONFIG_FILE)