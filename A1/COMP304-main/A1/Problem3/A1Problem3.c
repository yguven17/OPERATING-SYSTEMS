#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_SIZE 1000

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s number_of_children search_value\n", argv[0]);
        return 1;
    }

    int num_children = atoi(argv[1]);
    int search_value = atoi(argv[2]);

    int arr[MAX_SIZE];
    int arr_size = 0;
    int child_status;

    // Read sequence of numbers from stdin
    char input[10];
    while (fgets(input, sizeof(input), stdin) != NULL && arr_size < MAX_SIZE) {
        arr[arr_size] = atoi(input);
        arr_size++;
    }

    // Divide sequence among children
    int start = 0;
    int end;
    int i;
    for (i = 0; i < num_children; i++) {
        end = start + arr_size / num_children;
        if (i == num_children - 1) {
            end = arr_size; // Last child processes remainder
        }

        if (fork() == 0) {
            // Child process
            int j;
            for (j = start; j < end; j++) {
                if (arr[j] == search_value) {
                    printf("Found %d at index %d\n", search_value, j);
                    exit(0);
                }
            }
            exit(1);
        } else {
            // Parent process
            start = end;
        }
    }

    // Wait for a child process to finish
    while (wait(&child_status) > 0) {
        if (WIFEXITED(child_status)) {
            if (WEXITSTATUS(child_status) == 0) {
                // Success, kill other children and exit
                kill(0, SIGTERM);
                return 0;
            }
        }
    }

    // All children failed, print error message
    printf("Search value not found.\n");
    return 1;
}
