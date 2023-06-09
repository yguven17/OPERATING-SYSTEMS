#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <n>\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);

    pid_t pid;
    int level = 0;

    printf("Main Process ID: %d, level: %d\n", getpid(), level);

    for (int i = 1; i <= n; i++) {
        pid = fork();

        if (pid < 0) {
            fprintf(stderr, "Fork failed\n");
            return 1;
        } else if (pid == 0) {
            level++;
            printf("Process ID: %d, Parent ID: %d, level: %d\n", getpid(), getppid(), level);
        } else {
            wait(NULL);
        }
    }

    return 0;
}
