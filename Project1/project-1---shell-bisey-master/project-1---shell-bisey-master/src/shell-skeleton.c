#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
const char *sysname = "mishell";

#define MAX_DIRS 10
#define MAX_PATH_LENGTH 1024

char *dirs[MAX_DIRS];
int num_dirs = 0;

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			pch = strtok(NULL, splitters);
			if (pch) {
				command->redirects[redirect_index] = malloc(strlen(pch) + 1);
				strcpy(command->redirects[redirect_index], pch);
			}
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	buf[0] = 0;

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
			buf[index++] = '?'; // autocomplete
			break;
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);



int main() {
	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}

		free_command(command);
	}

	printf("\n");
	return 0;
}

char *locate_command_path(const char *command_name) { // locate command path
	char cmd[1024]; // command to execute
	snprintf(cmd, sizeof(cmd), "which %s",
			 command_name); // which command to find path
	FILE *fp = popen(cmd, "r"); // open pipe to execute command
	if (fp == NULL) { // if pipe is null
		return NULL; // return null
	}

	char path[1024]; // path to command
	if (fgets(path, sizeof(path), fp) == NULL) { // if path is null
		pclose(fp); // close pipe
		return NULL; // return null
	}

	path[strcspn(path, "\n")] = '\0'; // remove newline character from path
	pclose(fp); // close pipe

	char *result = malloc(
		strlen(path) +
		1); // allocate memory for the resulting path and copy the path into it.
	strcpy(result, path); // copy path into result

	return result;
}

void close_pipes(int *pipe_fds, int num_pipes) { // close pipes
	for (int i = 0; i < num_pipes * 2; i++) { // for each pipe
		close(pipe_fds[i]); // close pipe
	}
}

void setup_io_redirection(struct command_t *command) { // setup io redirection
	if (command->redirects[0]) { // input redirection
		int fd_in = open(command->redirects[0],
						 O_RDONLY); // open file for input redirection
		if (fd_in < 0) { // if file is not opened
			perror("Error opening file for input redirection"); // print error
			exit(EXIT_FAILURE); // exit
		}
		dup2(fd_in,
			 STDIN_FILENO); // duplicate file descriptor for input redirection
		close(fd_in); // close file
	}
	if (command->redirects[1]) { // output redirection
		int fd_out = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC,
						  0644); // open file for output redirection
		if (fd_out < 0) { // if file is not opened
			perror("Error opening file for output redirection"); // print error
			exit(EXIT_FAILURE); // exit
		}
		dup2(fd_out,
			 STDOUT_FILENO); // duplicate file descriptor for output redirection
		close(fd_out); // close file
	}
	if (command->redirects[2]) { // append redirection
		int fd_append = open(command->redirects[2],
							 O_WRONLY | O_CREAT | O_APPEND,
							 0644); // open file for append redirection
		if (fd_append < 0) { // if file is not opened
			perror("Error opening file for append redirection"); // print error
			exit(EXIT_FAILURE); // exit
		}
		dup2(fd_append,
			 STDOUT_FILENO); // duplicate file descriptor for append redirection
		close(fd_append); // close file
	}
}

// This function prints a list of recent directories.
void print_dirs() {
	// Print a heading for the list.
	printf("Recent directories:\n");
	// Loop through the array of directories.
	for (int i = 0; i < num_dirs; i++) {
		// Print the index of the directory (as a letter) and its name.
		printf("%c %d) %s\n", 'a' + i, i + 1, dirs[i]);
	}
}


void add_dir(char *dir) { // Define a function called add_dir that takes a pointer to a character as its argument
	//printf("Added directory: %s\n", dir); // Commented out line of code that prints a message to the console
	if (num_dirs == MAX_DIRS) { // Check if the number of directories is equal to the maximum allowed directories
		free(dirs[num_dirs - 1]); // Free the memory allocated for the last directory added to the array
		num_dirs--; // Decrement the number of directories
	}
	for (int i = num_dirs; i > 0; i--) { // Iterate over the directories in the array, starting from the last one
		dirs[i] = dirs[i - 1]; // Move each directory up one index in the array
	}
	if (strcmp(dir, "..") == 0) { // Check if the directory being added is ".."
		dirs[0] = "project-1---shell-bisey"; // Set the first directory in the array to a specific value
		//printf("The directory is equal to: %s\n", dirs[0]); // Commented out line of code that prints a message to the console
	} else {
		dirs[0] = dir; // Set the first directory in the array to the value of the argument passed to the function
		//printf("The directory is equal to: %s\n", dirs[0]); // Commented out line of code that prints a message to the console
	}
	num_dirs++; // Increment the number of directories in the array
}


void navigate_dir(char ch, char *dirs[]) { // Define a function called navigate_dir that takes a character and an array of character pointers as arguments
	printf("You entered the dir value '%c'.\n", ch); // Print a message to the console that displays the character entered by the user

	int index = -1; // Initialize the index variable to an invalid value

	if (ch >= 'a' && ch <= 'i') { // Check if the character is in the range of a to i
		index = ch - 'a' + 1; // Calculate the index based on the ASCII value of the character
	}
	if (ch >= 49 && ch <= 59) { // Check if the character is in the range of 1 to 9
		index = ch - 49 + 1; // Calculate the index based on the ASCII value of the character
	}

	char cwd[1024]; // Define a character array to store the current working directory

	if (getcwd(cwd, sizeof(cwd)) != NULL) { // Get the current working directory and check if it's valid
		printf("Current working dir before: %s\n", cwd); // Print a message to the console that displays the current working directory
	} else {
		perror("getcwd() error"); // Print an error message to the console if getcwd() fails
	}

	chdir(dirs[index-1]); // Change the current working directory to the directory at the calculated index
	if (getcwd(cwd, sizeof(cwd)) != NULL) { // Get the current working directory and check if it's valid
		printf("Current working dir after: %s\n", cwd); // Print a message to the console that displays the new current working directory
	} else {
		perror("getcwd() error"); // Print an error message to the console if getcwd() fails
	}
}


int is_dotfile(const char *name) { // Define a function called is_dotfile that takes a pointer to a character constant as an argument
	return name[0] == '.'; // Check if the first character of the name is a dot, indicating that it's a dotfile. Return 1 if it is, and 0 otherwise.
}

int is_binary(const char *path) { // Define a function called is_binary that takes a pointer to a character constant as an argument
	struct stat st; // Define a structure to hold information about a file
	if (stat(path, &st) != 0) { // Use the stat() function to get information about the file at the specified path. If stat() fails, return 0.
		return 0;
	}
	return (st.st_mode & S_IFMT) == S_IFREG && (st.st_mode & S_IXUSR) != 0; // Check if the file is a regular file (not a directory or other type) and if it's executable. Return 1 if both conditions are true, and 0 otherwise.
}


int count_lines(const char *path, const char *ext, int *blanks, int *comments,
				int *code) {
	// Check if the file at the specified path has the specified extension
	if (strcmp(path + strlen(path) - strlen(ext), ext) != 0) {
		return 0;
	}

	// Open the file at the specified path for reading
	FILE *file = fopen(path, "r");
	if (file == NULL) {
		return 0;
	}

	// Initialize counters for blank lines, comment lines, and code lines
	*blanks = 0;
	*comments = 0;
	*code = 0;

	// Define an enum to represent the possible states the parser can be in
	enum {
		CODE,
		LINE_COMMENT,
		BLOCK_COMMENT,
	} state = CODE;

	// Define a character array to hold each line of the file as it's read
	char line[1024];

	// Loop through each line of the file
	while (fgets(line, sizeof(line), file)) {
		// Count leading spaces or tabs as blank lines
		int i = 0;
		while (line[i] == ' ' || line[i] == '\t') {
			++i;
		}

		// Check if the line is empty or not
		if (line[i] == '\n') {
			++(*blanks);
		} else {
			// Depending on the current state of the parser, count the line as a comment or a line of code
			switch (state) {
			case CODE:
				++(*code);
				break;
			case LINE_COMMENT:
			case BLOCK_COMMENT:
				++(*comments);
				break;
			}
		}

		// Loop through each character in the current line
		while (line[i] != '\0' && line[i] != '\n') {
			// Depending on the current state of the parser and the characters in the line, update the state and counters
			switch (state) {
			case CODE:
				if (line[i] == '/' && line[i + 1] == '/') {
					state = LINE_COMMENT;
					++(*comments);
					i += 2;
				} else if (line[i] == '/' && line[i + 1] == '*') {
					state = BLOCK_COMMENT;
					++(*comments);
					i += 2;
				} else {
					++i;
				}
				break;
			case LINE_COMMENT:
				++i;
				break;
			case BLOCK_COMMENT:
				if (line[i] == '*' && line[i + 1] == '/') {
					state = CODE;
					i += 2;
				} else {
					++i;
				}
				break;
			}
		}
	}

	// Close the file and return 1 to indicate that the function ran successfully
	fclose(file);
	return 1;
}


// Function to handle the timebomb
void timebomb_handler() {
	kill(getppid(), SIGKILL); // sends SIGKILL signal to the parent process
	system("logout"); // executes the "logout" command to log out the current user
}

// Function to activate a timebomb that will terminate the parent process after a certain duration
int mishell_timebomb(int dur) {
	signal(SIGALRM, timebomb_handler); // registers the timebomb_handler function to be called when the alarm signal is received
	alarm(dur); // sets an alarm that will trigger the timebomb_handler function after the given duration
	for (int i = dur; i >= 0; i -= 1) {
		printf("WARNING: bomb active! %d seconds remaining.\n", i); // displays a warning message to the user with the remaining time
		sleep(1); // waits for 1 second
	}
	return SUCCESS; // returns a constant value to indicate success
}

int check_pid_exists(pid_t pid) {
	return kill(pid, 0) == 0 || errno != ESRCH;
}

int process_command(struct command_t *command) { // process command
	int r;

	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "bomb") == 0) { //kill your mishell
		//printf("%d\n",command->arg_count);
		if (command->arg_count < 3 ||
			command->arg_count > 3) { // if there are not 3 arguments
			printf("bomb: Works with one argument: %s <duration in seconds>\n",
				   command->name);
			return UNKNOWN;
		} else {
			//printf("%s\n",command->args[0]);
			//printf("%s\n",command->args[1]);
			int dur = atoi(command->args[1]);
			mishell_timebomb(dur);
		}
	}

	if (strcmp(command->name, "cd") == 0) {
		add_dir(strdup(command->args[1]));
		if (command->arg_count > 0) {
			r = chdir(command->args[1]);
			if (r == -1) {
				printf("-%s: %s: %s\n", sysname, command->name,
					   strerror(errno));
			}
			return SUCCESS;
		}
	}

	if (strcmp(command->name, "cloc") == 0) {
		if (command->arg_count < 3 || command->arg_count > 3) {
			printf("cloc: Works with one argument: %s '/directory'\n",
				   command->name);
			return UNKNOWN;
		} else {
			int file_count = 0, processed_count = 0;
			int total_blanks = 0, total_comments = 0, total_code = 0;
			int c_blanks = 0, c_comments = 0, c_code = 0;
			int cpp_blanks = 0, cpp_comments = 0, cpp_code = 0;
			int py_blanks = 0, py_comments = 0, py_code = 0;
			int txt_blanks = 0, txt_comments = 0, txt_code = 0;

			DIR *dir = opendir(command->args[1]);
			if (dir == NULL) {
				perror(command->args[0]);
				return 1;
			}
			struct dirent *entry;
			while ((entry = readdir(dir)) != NULL) {
				if (is_dotfile(entry->d_name)) {
					continue;
				}

				char path[512];
				snprintf(path, sizeof(path), "%s/%s", command->args[0],
						 entry->d_name);

				if (is_binary(path)) {
					continue;
				}

				++file_count;
				int blanks, comments, code;
				if (count_lines(path, ".c", &blanks, &comments, &code)) {
					printf(".c counting");
					++processed_count;
					c_blanks += blanks;
					c_comments += comments;
					c_code += code;
				} else if (count_lines(path, ".cpp", &blanks, &comments,
									   &code)) {
					printf(".cpp counting");
					++processed_count;
					cpp_blanks += blanks;
					cpp_comments += comments;
					cpp_code += code;
				} else if (count_lines(path, ".py", &blanks, &comments,
									   &code)) {
					printf(".py counting");
					++processed_count;
					py_blanks += blanks;
					py_comments += comments;
					py_code += code;
				} else if (count_lines(path, ".txt", &blanks, &comments,
									   &code)) {
					printf(".txt counting");
					++processed_count;
					txt_blanks += blanks;
					txt_comments += comments;
					txt_code += code;
				}
				total_blanks += blanks;
				total_comments += comments;
				total_code += code;
			}

			printf("%d text files.\n", file_count);
			printf("%d unique files.\n", processed_count);
			printf("%d files ignored.\n", file_count - processed_count);
			printf(
				"------------------------------------------------------------------------\n");
			printf(
				"Language          files             blank       comment             code\n");
			printf(
				"------------------------------------------------------------------------\n");
			printf(
				"C                 %5d           %5d           %5d           %5d\n",
				c_blanks + c_comments + c_code, c_blanks, c_comments, c_code);
			printf(
				"C++               %5d           %5d           %5d           %5d\n",
				cpp_blanks + cpp_comments + cpp_code, cpp_blanks, cpp_comments,
				cpp_code);
			printf(
				"Python            %5d           %5d           %5d           %5d\n",
				py_blanks + py_comments + py_code, py_blanks, py_comments,
				py_code);
			printf(
				"Text              %5d           %5d           %5d           %5d\n",
				txt_blanks + txt_comments + txt_code, txt_blanks, txt_comments,
				txt_code);
			printf(
				"-----------------------------------------------------------------------\n");
			printf(
				"Total             %5d           %5d           %5d           %5d\n",
				total_blanks + total_comments + total_code, total_blanks,
				total_comments, total_code);
			printf(
				"-----------------------------------------------------------------------\n");
			return 0;
		}
	}

	if (strcmp(command->name, "cdh") == 0) { // cdh command
		if (command->arg_count > 0) {
			print_dirs();
			char ch;
			printf("Select directory by letter or number: ");
			scanf("%c", &ch);
			//printf("You entered the character '%c'.\n", ch);
			//printf("dirs '%s'.\n", dirs[1]);
			navigate_dir(ch, dirs);
			return SUCCESS;
		}
	}

	if (strcmp(command->name, "roll") == 0) { // roll command

		if (command->arg_count < 3 ||
			command->arg_count > 3) { // if there are not 3 arguments
			printf("roll: Works with one argument: %s 'digit'd'digit'\n",
				   command->name); // print error
			return UNKNOWN; // return unknown
		} else { // if there are 3 arguments
			int num_rolls = 1; // number of rolls
			int num_sides; // number of sides on dice
			char *token =
				strtok(command->args[1], "d"); // split string into tokens
			if (atoi(&token[2]) == 0) { // if the first token is a number
				num_sides =
					atoi(&token[0]); // set number of sides to first token
			} else { // if the first token is not a number
				num_rolls = atoi(token); // set number of rolls to first token
				token = strtok(NULL, "d"); // split string into tokens again
				num_sides = atoi(token); // set number of sides to second token
			}
			int result = 0; // result of roll
			int *rolls; // array of rolls
			rolls = (int *)malloc(num_rolls *
								  sizeof(int)); // allocate memory for rolls
			for (int i = 0; i < num_rolls; i++) { // for each number of rolls
				rolls[i] = rand() % num_sides + 1; // roll dice
				result += rolls[i]; // add roll to result
			}

			// If rolled once
			if (num_rolls == 1) {
				printf("Rolled %d", result); // print result
			} else { // if rolled more than once
				printf("Rolled %d (", result); // print result
				for (int i = 0; i < num_rolls; i++) { // for each roll
					printf("%d", rolls[i]); // print roll
					if (i < num_rolls - 1) { // if there are more rolls
						printf(" + "); // print plus sign
					}
				}
				printf(")\n"); // print new line
			}
			free(rolls); // free memory
			return SUCCESS; // return success
		}
	}

	

	// Count the number of commands in the linked list
	int num_commands = 0; // number of commands in linked list
	struct command_t *current_command =
		command; // current command in linked list
	while (current_command) { // while there is a current command
		num_commands++; // increment number of commands
		current_command =
			current_command->next; // set current command to next command
	}

	// Allocate memory for pipe file descriptors
	int *pipe_fds =
		malloc(sizeof(int) * 2 *
			   (num_commands - 1)); // allocate memory for pipe file descriptors
	for (int i = 0; i < num_commands - 1;
		 i++) { // for each command in linked list
		if (pipe(pipe_fds + 2 * i) < 0) { // if pipe is not created
			perror("pipe"); // print error
			return UNKNOWN; // return unknown
		}
	}

	int command_index = 0; // command index
	current_command =
		command; // set current command to first command in linked list
	while (current_command) { // while there is a current command
		pid_t pid = fork(); // fork process
		if (pid == 0) { // if child process

			// Redirect input and output
			setup_io_redirection(
				current_command); // set up input and output redirection

			// Set up pipes for the command
			if (command_index > 0) { // if there is a previous command
				dup2(
					pipe_fds[2 * (command_index - 1)],
					STDIN_FILENO); // duplicate file descriptor for input redirection
			}
			if (command_index <
				num_commands - 1) { // if there is a next command
				dup2(
					pipe_fds[2 * command_index + 1],
					STDOUT_FILENO); // duplicate file descriptor for output redirection
			}

			close_pipes(pipe_fds, num_commands - 1); // close pipes

			char *command_path = locate_command_path(
				current_command->name); // Execute the command
			if (!command_path) { // If the command was not found
				printf("-%s: %s: %s\n", sysname, current_command->name,
					   strerror(errno)); // Print error message
				exit(EXIT_FAILURE); // Exit the child process
			}
			int x = execv(command_path,
						  current_command->args); // Execute the command
			if (x == -1) { // If the command was not found
				printf("-%s: %s: %s\n", sysname, current_command->name,
					   strerror(errno)); // Print error message
				exit(EXIT_FAILURE); // Exit the child process
			}
		} else if (pid < 0) { // If fork failed
			perror("fork"); // Print error message
			return UNKNOWN; // Return unknown
		}

		current_command = current_command->next; // Move to the next command
		command_index++; // Increment the command index
	}

	close_pipes(pipe_fds,
				num_commands -
					1); // Close all pipe file descriptors in the parent process
	free(
		pipe_fds); // Free the memory allocated for pipe file descriptors in the parent process

	if (!command->background) { // If the command is not run in the background
		for (int i = 0; i < num_commands; i++) {
			waitpid(-1, NULL,
					0); // Wait for all child processes to finish executing
		}
	}
	return SUCCESS; // Return success if all commands were executed successfully
}
