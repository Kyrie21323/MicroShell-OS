# Compiler and flags
CC      := gcc
# Add -pthread for threads
CFLAGS  := -std=c11 -Wall -Wextra -O2 -pthread
# Define POSIX for strtok_r and other POSIX functions
CFLAGS += -D_POSIX_C_SOURCE=200809L
# Add -pthread for linking
LDFLAGS := -pthread

INCDIR  := include
SRCDIR  := src
OBJDIR  := build

TARGETS := myshell server client

INCLUDES := -I$(INCDIR)

# Source files for the original shell
SHELL_SRC := \
  $(SRCDIR)/main.c \
  $(SRCDIR)/parse.c \
  $(SRCDIR)/exec.c  \
  $(SRCDIR)/redir.c \
  $(SRCDIR)/tokenize.c \
  $(SRCDIR)/util.c

# Source files for server (includes shell modules + server + net)
SERVER_SRC := \
  $(SRCDIR)/parse.c \
  $(SRCDIR)/exec.c  \
  $(SRCDIR)/redir.c \
  $(SRCDIR)/tokenize.c \
  $(SRCDIR)/util.c \
  $(SRCDIR)/net.c \
  $(SRCDIR)/server.c

# Source files for client (includes net + client)
CLIENT_SRC := \
  $(SRCDIR)/net.c \
  $(SRCDIR)/client.c

# Object files
SHELL_OBJ := $(SHELL_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
SERVER_OBJ := $(SERVER_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
CLIENT_OBJ := $(CLIENT_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

.PHONY: all clean run run-server run-client

all: $(TARGETS)

# Build original shell
myshell: $(SHELL_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

# Build server
server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

# Build client
client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: myshell
	./myshell

run-server: server
	./server 5050

run-client: client
	./client 127.0.0.1 5050

clean:
	rm -rf $(OBJDIR) $(TARGETS)
