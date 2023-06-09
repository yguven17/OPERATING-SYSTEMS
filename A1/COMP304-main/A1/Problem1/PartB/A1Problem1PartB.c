#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    pid_t child_pid = fork();

    if (child_pid == 0) {
        // Child process
        exit(0);
    } else if (child_pid > 0) {
        // Parent process
        sleep(5); // Wait for 5 seconds
        wait(NULL); // Reap the child process
    } else {
        // Fork error
        printf("Fork failed\n");
        exit(1);
    }

    return 0;
}
