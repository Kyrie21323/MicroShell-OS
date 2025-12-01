#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <seconds>\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    for (int i = 0; i < n; i++) {
        // Output format matching screenshot: "Demo i/N"
        printf("Demo %d/%d\n", i + 1, n);
        fflush(stdout); 
        // Note: The SERVER simulates the sleep. 
        // This program is just for manual testing or if executed directly.
        // In the Phase 4 logic, the server 'simulates' running this 
        // by sleeping itself, but we provide this file as requested.
    }
    return 0;
}
