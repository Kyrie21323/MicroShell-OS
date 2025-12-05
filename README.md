# MicroShell-OS - Unix Shell & Networked Job Scheduler

A comprehensive Operating Systems project implementing a Unix-like shell with command parsing, I/O redirection, pipelines, and a networked client-server job scheduler with advanced CPU scheduling algorithms.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Building the Project](#building-the-project)
- [Components](#components)
  - [Standalone Shell (mysh)](#standalone-shell-mysh)
  - [Server (Networked Job Scheduler)](#server-networked-job-scheduler)
  - [Client](#client)
  - [Demo Program](#demo-program)
- [Usage](#usage)
- [Scheduling Algorithm](#scheduling-algorithm)
- [Project Structure](#project-structure)
- [Technical Details](#technical-details)

---

## Overview

This project consists of four main components:

1. **`mysh`** - A standalone Unix-like shell interpreter
2. **`server`** - A multi-threaded networked job scheduler
3. **`client`** - A TCP client for remote command execution
4. **`demo`** - A test program for demonstrating job scheduling

The project demonstrates core Operating Systems concepts including process management, inter-process communication, socket programming, multi-threading, and CPU scheduling algorithms.

---

## Features

### Shell Features
- **Command Execution**: Execute external programs using `fork()` and `execvp()`
- **Quote Handling**: Support for single (`'`) and double (`"`) quotes with proper escaping
- **I/O Redirection**:
  - Input redirection: `< filename`
  - Output redirection (truncate): `> filename`
  - Output redirection (append): `>> filename`
  - Error redirection: `2> filename`
- **Pipelines**: Multi-stage command pipelines using `|` operator
- **Wildcard Expansion**: Glob pattern matching (`*`, `?`, `[]`)

### Server Features
- **Multi-client Support**: Handles multiple simultaneous client connections
- **Thread-per-Client Model**: Each client is handled by a dedicated thread
- **Dual Job Queues**:
  - Shell command queue (FIFO, absolute priority)
  - Demo/program job queue (RR + SRJF scheduling)
- **Preemptive Scheduling**: Shorter jobs can preempt running jobs
- **Timeline Tracking**: Execution summary with Gantt chart-style output

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         SERVER                                   │
│  ┌─────────────────┐    ┌─────────────────────────────────────┐ │
│  │  Accept Thread  │───▶│         Client Handler Threads      │ │
│  │  (Main Loop)    │    │  [Client 1] [Client 2] [Client N]   │ │
│  └─────────────────┘    └─────────────────────────────────────┘ │
│           │                            │                         │
│           │                            ▼                         │
│           │              ┌─────────────────────────────────────┐ │
│           │              │           Job Queues                │ │
│           │              │  ┌─────────────────────────────┐    │ │
│           │              │  │ Shell Queue (FIFO, Priority)│    │ │
│           │              │  └─────────────────────────────┘    │ │
│           │              │  ┌─────────────────────────────┐    │ │
│           │              │  │   Demo Queue (RR + SRJF)    │    │ │
│           │              │  └─────────────────────────────┘    │ │
│           │              └─────────────────────────────────────┘ │
│           │                            │                         │
│           │                            ▼                         │
│           │              ┌─────────────────────────────────────┐ │
│           └─────────────▶│        Scheduler Thread             │ │
│                          │   (Selects & Executes Jobs)         │ │
│                          └─────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                                    │
                        TCP Socket (Port 8080)
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
        ▼                           ▼                           ▼
   ┌─────────┐                 ┌─────────┐                 ┌─────────┐
   │ Client 1│                 │ Client 2│                 │ Client N│
   └─────────┘                 └─────────┘                 └─────────┘
```

---

## Building the Project

### Prerequisites
- GCC compiler
- POSIX-compliant system (Linux/Unix/macOS)
- pthread library

### Compilation

```bash
# Build all components
make

# Build individual components
make mysh      # Standalone shell
make server    # Networked scheduler
make client    # Network client
make demo      # Demo program

# Clean build artifacts
make clean
```

---

## Components

### Standalone Shell (mysh)

A fully-featured Unix-like shell that runs locally without networking.

```bash
./mysh
$ ls -la
$ echo "Hello World" > output.txt
$ cat < input.txt | grep pattern | wc -l
$ exit
```

**Supported Commands:**
- Any external program in `PATH`
- Built-in: `exit`

**Redirection Examples:**
```bash
$ command < input.txt          # Input from file
$ command > output.txt         # Output to file (truncate)
$ command >> output.txt        # Output to file (append)
$ command 2> error.txt         # Stderr to file
$ cmd1 | cmd2 | cmd3           # Pipeline
```

### Server (Networked Job Scheduler)

A multi-threaded server that accepts client connections and schedules command execution.

```bash
./server
# Server starts on port 8080
# Output:
# -------------------------
# | Hello, Server Started |
# -------------------------
```

**Server Logging Format:**
```
[client_id] >>> command         # Command received
(client_id) --- created (N)     # Job created with burst time N
(client_id) --- started (N)     # Job started execution
(client_id) --- running (N)     # Job resumed after preemption
(client_id) --- waiting (N)     # Job preempted, N time remaining
(client_id) --- ended (0)       # Job completed
[client_id] <<< N bytes sent    # Output sent to client
```

### Client

A simple TCP client that connects to the server and sends commands.

```bash
./client
$ ls -la                # Execute shell command
$ demo 10               # Run demo program for 10 seconds
$ exit                  # Disconnect
```

### Demo Program

A test program that simulates CPU-bound jobs with configurable duration.

```bash
./demo 5
# Output:
# Demo 0/5
# Demo 1/5
# Demo 2/5
# Demo 3/5
# Demo 4/5
# Demo 5/5
```

---

## Scheduling Algorithm

The server implements a **hybrid scheduling algorithm** with two priority levels:

### Priority Level 1: Shell Commands (Absolute Priority)
- **Algorithm**: FIFO (First-In-First-Out)
- **Behavior**: Execute immediately to completion
- **Preemption**: Can preempt any demo job
- **Burst Time**: -1 (instant execution)

### Priority Level 2: Demo/Program Jobs
- **Algorithm**: Round Robin (RR) + Shortest Remaining Job First (SRJF)
- **Time Quantum**:
  - First quantum: 3 seconds
  - Subsequent quanta: 7 seconds
- **Preemption Rules**:
  - Shell commands always preempt demo jobs
  - Newer demo jobs with strictly shorter remaining time can preempt
- **Fairness Rule**: No same job twice consecutively (when alternatives exist)

### Timeline Output
When preemption occurs, the server prints a Gantt chart-style summary:
```
0)-P1-(3)-P2-(6)-P1-(10)
```
This shows: P1 ran until time 3, P2 ran until time 6, P1 finished at time 10.

---

## Project Structure

```
Terminal/
├── Makefile                    # Build configuration
├── README.md                   # This file
├── myshell.c                   # Legacy standalone shell (single file)
├── include/                    # Header files
│   ├── errors.h                # Error message definitions
│   ├── exec.h                  # Execution function declarations
│   ├── job.h                   # Job structure definition
│   ├── net.h                   # Network function declarations
│   ├── parse.h                 # Parser function declarations
│   ├── redir.h                 # Redirection function declarations
│   ├── tokenize.h              # Tokenizer declarations
│   └── util.h                  # Utility function declarations
├── src/                        # Source files
│   ├── main.c                  # Standalone shell entry point
│   ├── server.c                # Server with job scheduler
│   ├── client.c                # Network client
│   ├── demo.c                  # Demo test program
│   ├── exec.c                  # Command execution logic
│   ├── parse.c                 # Command parsing & validation
│   ├── tokenize.c              # Quote-aware tokenization & globbing
│   ├── redir.c                 # I/O redirection setup
│   ├── net.c                   # Socket networking utilities
│   └── util.c                  # String utilities
└── server.log                  # Server log file (generated)
```

---

## Technical Details

### Command Parsing (`parse.c`, `tokenize.c`)
- **Tokenization**: Quote-aware parsing that respects single/double quotes
- **Escape Sequences**: Supports `\"` and `\\` within double quotes
- **Validation**: Checks for unclosed quotes, missing redirection targets, invalid pipeline syntax

### Process Management (`exec.c`)
- Uses `fork()` to create child processes
- `execvp()` for executing programs with PATH search
- `wait()` / `waitpid()` for synchronization
- Captures stdout/stderr via pipes for network transmission

### Pipeline Implementation
- Creates N-1 pipes for N-stage pipeline
- Each stage runs in separate child process
- Proper file descriptor management with `dup2()`
- First stage reads from `/dev/null` to prevent blocking

### Networking (`net.c`)
- **Protocol**: Length-prefixed messages (4-byte network order length + data)
- **End Marker**: `<<EOF>>` signals end of command output
- **Socket Options**: `SO_REUSEADDR` for quick server restart

### Thread Synchronization (`server.c`)
- **Mutex**: Protects job queues from race conditions
- **Condition Variable**: Wakes scheduler when jobs arrive
- **Signal Handling**: Graceful shutdown on `SIGINT` (Ctrl+C)

### Job Structure
```c
typedef struct Job {
    int id;              // Unique job ID
    int client_id;       // Client who submitted the job
    int client_fd;       // Socket for sending output
    char *command;       // Raw command string
    JobType type;        // JOB_CMD or JOB_DEMO
    int initial_burst;   // Original burst time (N or -1)
    int remaining_time;  // Time left to execute
    int rounds_run;      // Quantum tracking
    int bytes_sent;      // Output statistics
    int arrival_seq;     // Arrival order for SRJF
    int run_epoch_seq;   // Preemption tracking
    struct Job *next;    // Queue linkage
} Job;
```
