#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_CHILDREN 100

int main(int argc, char *argv[]) {
    int num_children = atoi(argv[1]);
    char *cmd = argv[2];
    int i, status;
    pid_t pid;
    struct timeval start_time[MAX_CHILDREN], end_time[MAX_CHILDREN];
    double elapsed_time[MAX_CHILDREN];
    int pipefd[MAX_CHILDREN][2];

    // Create pipes for communication with child processes
    for (i = 0; i < num_children; i++) {
        if (pipe(pipefd[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < num_children; i++) {
        pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {  // Child process
            close(pipefd[i][0]); // Close unused read end
            struct timeval start, end;
            gettimeofday(&start, NULL); // Record start time
            pid_t pid2 = fork();
            if (pid2 == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else if (pid2 == 0) { // Grandchild process
                // Redirect stdout and stderr to /dev/null to suppress output
                freopen("/dev/null", "w", stdout);
                freopen("/dev/null", "w", stderr);
                // Execute command
                execlp(cmd, cmd, NULL);
                perror("execlp");
                exit(EXIT_FAILURE);
            } else { // Child process
                wait(&status);
                gettimeofday(&end, NULL); // Record end time
                double elapsed = (end.tv_sec - start.tv_sec) * 1000.0; // Convert to milliseconds
                elapsed += (end.tv_usec - start.tv_usec) / 1000.0; // Convert to milliseconds
                write(pipefd[i][1], &elapsed, sizeof(double)); // Send elapsed time to parent process
                close(pipefd[i][1]); // Close write end
                exit(EXIT_SUCCESS);
            }
        }
    }

    // Parent process
    double total_time = 0.0;
    double max_time = 0.0;
    double min_time = __DBL_MAX__;
    for (i = 0; i < num_children; i++) {
        close(pipefd[i][1]); // Close unused write end
        read(pipefd[i][0], &elapsed_time[i], sizeof(double)); // Read elapsed time from child process
        total_time += elapsed_time[i];
        if (elapsed_time[i] > max_time) {
            max_time = elapsed_time[i];
        }
        if (elapsed_time[i] < min_time) {
            min_time = elapsed_time[i];
        }
        close(pipefd[i][0]); // Close read end
        printf("Child %d Executed in %.2f millis\n", i+1, elapsed_time[i]);
    }
    printf("Max: %.2f millis\n", max_time);
    printf("Min: %.2f millis\n", min_time);
    printf("Average: %.2f millis\n", (float)total_time / num_children);

    return 0;
}