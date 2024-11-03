# Compiler and flags
CC = gcc
CFLAGS = -Wall -Iinclude -std=c11 $(shell pkg-config --cflags gtk+-3.0) -I/usr/include/hiredis
LDFLAGS = $(shell pkg-config --libs gtk+-3.0) -lhiredis

# Directories
OBJDIR = obj
SRCDIR = src
EXEC = FourChanArchiver

# Source and object files
SRC = $(wildcard $(SRCDIR)/*.c)
OBJ = $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Default target
all: $(EXEC)

# Linking
$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

# Compiling each .c file to .o in the obj directory
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure obj directory exists
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Clean up build files
clean:
	rm -rf $(OBJDIR) $(EXEC)
