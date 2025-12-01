CC = gcc
# -Iinclude tells the compiler to look for header files in the 'include' folder
CFLAGS = -Wall -Wextra -pthread -g -Iinclude

# Define source directory
S = src

all: mysh server client demo

# 1. mysh (Standalone Shell)
mysh: $S/main.c $S/parse.c $S/exec.c $S/tokenize.c $S/util.c $S/redir.c
	$(CC) $(CFLAGS) -o mysh $S/main.c $S/parse.c $S/exec.c $S/tokenize.c $S/util.c $S/redir.c

# 2. server (Networked Scheduler)
server: $S/server.c $S/parse.c $S/exec.c $S/tokenize.c $S/util.c $S/net.c $S/redir.c
	$(CC) $(CFLAGS) -o server $S/server.c $S/parse.c $S/exec.c $S/tokenize.c $S/util.c $S/net.c $S/redir.c

# 3. client (Network Client)
client: $S/client.c $S/net.c
	$(CC) $(CFLAGS) -o client $S/client.c $S/net.c

# 4. demo (Test Program)
demo: $S/demo.c
	$(CC) $(CFLAGS) -o demo $S/demo.c

clean:
	rm -f mysh server client demo *.o
