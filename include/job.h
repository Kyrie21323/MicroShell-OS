#ifndef JOB_H
#define JOB_H

typedef enum {
    JOB_CMD,    // Shell command (-1 burst)
    JOB_DEMO    // Demo program (N burst)
} JobType;

typedef struct Job {
    int id;                 // Unique Job ID
    int client_id;          // ID of the client who sent it
    int client_fd;          // Socket to send output back to
    char *command;          // The raw command string
    JobType type;           // CMD or DEMO
    int initial_burst;      // N (or -1)
    int remaining_time;     // Decrements as it runs
    int rounds_run;         // To track Quantum (3s vs 7s)
    struct Job *next;       // For Linked List
} Job;

#endif
