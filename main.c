#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <ctype.h>

/*
 * Enhanced Shell by Breanna Zinky 2/15/2024
 * This program acts as a simple shell that
 * loops to take and run user commands.
 * It has added functionality to allow
 * piping commands, output and input redirection > and <,
 * history logs, and redoing commands.
 *
 */

#define MAX_PIPED_COMMANDS 32
#define MAX_COMMAND_SIZE 1024
#define MAX_STRING_SIZE 256
#define MAX_HISTORY_SIZE 6

int main() {
    /////////////////////// VARIABLE INITIALIZATION //////////////////
    int breakLoop = 0; // Flag to break out of main loop- to quit the shell

    // Variables for redirection features; < and > and >!
    char before[MAX_STRING_SIZE], after[MAX_STRING_SIZE]; // For output redirection string processing
    char before2[MAX_STRING_SIZE], after2[MAX_STRING_SIZE]; // For input redirection string processing
    int outputFile = 0; // Flag if user command has > output redirection
    int inputFile = 0; // Flag if user command has < input redirection
    char *fileName;// File name from piping commands (for output redirection >)
    char *finalCommand; // Just the command, excluding the file name, from output redirection commands
    char *fileNameInput; // File name from piping commands (for input redirection <)
    char *finalCommand2; // Just the command, exlucing the file name, from input redirection commands
    int forcedRedirection = 0; // Flag if the user entered >! for forced output redirection, to overwrite files

    // Variables for history feature
    char *history[MAX_HISTORY_SIZE]; // List to store a history of commands. Acts as a circular buffer.
    int historyBufferLength = 0; // Amount of items in history list
    int historyReadIndex = 0; // Index to read from in history list
    int historyWriteIndex = 0; // Index to write to in history list

    pid_t childPids[MAX_PIPED_COMMANDS]; // Array of child PIDs (since with piping commands multiple children can run at once).
    int anyChildFailed = 0; // Flag for if any child process in a piped command failed.

    /////////////////// CORE SHELL LOOP /////////////////////
    while (1) {
	// Reset flags
	anyChildFailed = 0;

	///////////// USER INPUT AND INPUT STRING PROCESSING /////////////////////
	// Prompt the user for a command
	printf("Enter a command: ");
	fflush(stdout);

        // Read a line into a character array
        char fullCommand[MAX_COMMAND_SIZE]; // Fixed size for fgets
        fgets(fullCommand, sizeof(fullCommand), stdin);

	////////////////// REDO HISTORY COMMAND HANDLING ////////////////////
        // Check the input for a redo history command ex. !2 executes the 2nd command in the history queue
        // Note: Only checks the first two characters of the first word in the command
        // If the command is a redo history command, just replace the command with the one from history
        if (fullCommand[0] == '!' && isdigit(fullCommand[1])){
        	// Convert char from input command to integer
                int charToInt = (int)(fullCommand[1] - '0');
                // Now replace fullCommand with the old one from history
                // Note: It won't be possible to pipe a redo history command, ex. can't do !2 | wc
                strcpy(fullCommand, history[(historyWriteIndex + (MAX_HISTORY_SIZE - charToInt)) % MAX_HISTORY_SIZE]);
        }

	// Make a copy of fullCommand for purposes to store in history (need a copy since strtok modifies it)
	char fullCommand2[MAX_COMMAND_SIZE];
        strcpy(fullCommand2, fullCommand);
	
	// Tokenize by | and store into an array
	char *commands[MAX_PIPED_COMMANDS];
	char *pipeCommand = strtok(fullCommand, "|");
	// Place each separate pipe command into an array
	int numCommands = 0;
	while (pipeCommand != NULL){
		commands[numCommands++] = pipeCommand;
		pipeCommand = strtok(NULL, "|");
	}
	
	/////////////// PIPE INITIALIZATION //////////////////
	int pipes[numCommands - 1][2]; // Create one less pipe than the number of commands
	if (numCommands > 1){
		for (int i = 0; i < numCommands - 1; i++){
			if (pipe(pipes[i]) == -1){
				perror("pipe error");
				exit(1);
			}
		}
	}

	///////////// LOOP THROUGH FOR EACH PIPED COMMAND /////////////////// 
	for (int commandIndex = 0; commandIndex < numCommands; commandIndex++){		  		
		////////////// STRING PROCESSING /////////////////
		// Create copies of the command since strtok modifies the original string
		char *copiedCommand = malloc(strlen(commands[commandIndex]) + 1); // +1 for the null terminator
		strcpy(copiedCommand, commands[commandIndex]);
		char *copiedCommand2 = malloc(strlen(commands[commandIndex]) + 1); // +1 for the null terminator
                strcpy(copiedCommand2, commands[commandIndex]);
		
		////////////// STD OUTPUT REDIRECTION > HANDLING ////////////////
		// Check the command for std output redirection to a file >
		// If > exists in the string, we will want to create two strings - the command and the file name
		// Make sure to sanitize the strings to remove space before and after
		// Check if the file already exists, if not, we will change the std output to be the file
		forcedRedirection = 0; // Reset forced redirection flag
		char *outputRedirectionIndex = strchr(commands[commandIndex], '>'); // Check for > character in command string
		if (outputRedirectionIndex != NULL){ // Case: > found
			outputFile = 1; // flag to output to file = true
		
			////// String Processing //////
                        *outputRedirectionIndex = '\0';
                        // Copy the part before "<" into the command string
                        strcpy(before, commands[commandIndex]);

			// Check if the next character is '!' for forced output redirection
			if (*(outputRedirectionIndex + 1) == '!'){
				forcedRedirection = 1; // Set flag for forced redirection
				// Copy the part after "<!" into the file name string
				strcpy(after, outputRedirectionIndex + 2);
			} else {
			// Copy the part after "<" into the file name string
                        strcpy(after, outputRedirectionIndex + 1);
			}
			
			// Remove leading and trailing spaces from the strings
			// Remove leading spaces
    			finalCommand = before;
   	 		while (isspace(*finalCommand)) {
        		finalCommand++; // Move the pointer past leading spaces
    			}
    			// Remove trailing spaces
    			char *end = before + strlen(before) - 1;
    			while (end > finalCommand && isspace(*end)) {
        			*end = '\0'; // Replace trailing spaces with null terminator
        			end--; // Move the pointer towards the beginning of the string
   	 		}
			fileName = after;
                        while (isspace(*fileName)) {
                        fileName++; // Move the pointer past leading spaces
                        }
                        // Remove trailing spaces
                        char *end2 = after + strlen(after) - 1;
                        while (end2 > fileName && isspace(*end2)) {
                                *end2 = '\0'; // Replace trailing spaces with null terminator
                                end2--; // Move the pointer towards the beginning of the string
                        }

			// Replace the copiedCommand with the command before the > now
			strcpy(copiedCommand, finalCommand);
			strcpy(copiedCommand2, finalCommand); 
		} else (outputFile = 0); // If no > found on the current command, set flag to output to file to false

		////////////// STD INPUT REDIRECTION < HANDLING ////////////////
                // Check the command for std input redirection to a file <
                // If < exists in the string, we will want to create two strings - the command and the file name
                // Make sure to sanitize the strings to remove space before and after
                // Check if the file actually exists otherwise throw an error
                char *inputRedirectionIndex = strchr(commands[commandIndex], '<'); // Check for > character in command string
                if (inputRedirectionIndex != NULL){ // Case: < found
                        inputFile = 1; // flag to output to file = true

                        ////// String Processing //////
                        *inputRedirectionIndex = '\0';
                        // Copy the part before "<" into the command string
                        strcpy(before2, commands[commandIndex]);
                        // Copy the part after "<" into the file name string
                        strcpy(after2, inputRedirectionIndex + 1);

                        // Remove leading and trailing spaces from the strings
                        // Remove leading spaces
                        finalCommand2 = before2;
                        while (isspace(*finalCommand2)) {
                        finalCommand2++; // Move the pointer past leading spaces
                        }
                        // Remove trailing spaces
                        char *end3 = before2 + strlen(before2) - 1;
                        while (end3 > finalCommand2 && isspace(*end3)) {
                                *end3 = '\0'; // Replace trailing spaces with null terminator
                                end3--; // Move the pointer towards the beginning of the string
                        }
                        fileNameInput = after2;
                        while (isspace(*fileNameInput)) {
                        fileNameInput++; // Move the pointer past leading spaces
                        }
                        // Remove trailing spaces
                        char *end4 = after2 + strlen(after2) - 1;
                        while (end4 > fileNameInput && isspace(*end4)) {
                                *end4 = '\0'; // Replace trailing spaces with null terminator
                                end4--; // Move the pointer towards the beginning of the string
                        }
		
                        // Replace the copiedCommand with the command before the > now
                        strcpy(copiedCommand, finalCommand2);
                        strcpy(copiedCommand2, finalCommand2);
                } else (inputFile = 0); // If no > found on the current command, set flag to input from file to false

		///////////////// MORE STRING PROCESSING ////////////////////
		// Tokenize the single command with strtok()
		char *argCount = strtok(copiedCommand, " \n\t"); // Delimiter is space or newline
        	// Loop through the tokens, get number of tokens
        	int numTokens = 0;
        	while (argCount != NULL){
                	numTokens++;
                	argCount = strtok(NULL, " \n\t");
        	}
        	// Now create an array of this size and add the tokens to the array
        	char *arg = strtok(copiedCommand2, " \n\t");
        	char *arguments[numTokens + 1]; // + 1 for null at end
        	int i = 0;
		while (arg != NULL){
                	arguments[i++] = arg;
                	arg = strtok(NULL, " \n\t");
        	}
        	arguments[i] = NULL;

		///////////////////////// CHECK QUIT CONDITION /////////////////////////
        	// Parse the command to check for quit - works if "quit" is the first word
        	if (strcmp(arguments[0], "quit") == 0){
			breakLoop = 1;	
			break; 
        	}

		//////////////////////////// FORK CHILDREN ///////////////////////////
        	// Spawn a child process to execute the command
		childPids[commandIndex] = fork(); // Store each child PID in an array for the parent to see them all

        	if (childPids[commandIndex] < 0){ // Case: fork error
			perror("fork failure");
                	exit(1);
        	}

		///////////////////////// CHILD PROCESS ////////////////////////
        	else if (childPids[commandIndex] == 0){ // Case: child
			
			//////////////////// HANDLE OUTPUT REDIRECTION > //////////////////
			// Check if current command has output redirection >
			if (outputFile == 1){
				// Check if file exists already
				if (access(fileName, F_OK) != -1 && forcedRedirection == 0) { // CASE: File already exists
					fprintf(stderr, "File write error. File already exists.\n");
					exit(1);
                        	} else { // CASE: File does not already exist
                                 	// Open new file
                                 	int filefd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                 	if (filefd == -1){
                                               perror("Failure to open file.");
                                               exit(1);
                                        }
					// Redirect std output to the file
                                 	if (dup2(filefd, STDOUT_FILENO) == -1){
						perror("dup failure.");
						exit(1);
					}
					if (filefd != STDOUT_FILENO){
                                 		close(filefd);
					}
                        	}
			}

			///////////////////////// HANDLE INPUT REDIRECTION < ////////////////
			// Check if current command has input redirection <
			if (inputFile == 1){
				// Check if file exists already
                                if (access(fileNameInput, F_OK) != -1) { // CASE: File already exists
                                        int filefd2 = open(fileNameInput, O_RDONLY);
					if (filefd2 == -1){
						perror("Failure to open file.");
						exit(1);
					}
					// Redirect in std input from file
					if (dup2(filefd2, STDIN_FILENO) == -1){ // isntead of STDIN_FILENO
						perror("dup failure.");
						exit(1);
					}
					if (filefd2 != STDIN_FILENO){
						close(filefd2);
					}
                                } else { // CASE: File does not already exist
					fprintf(stderr, "File read error. File does not exist.\n");
                                	exit(1);
				}
			}

			////////////////////////// MANAGE PIPES //////////////////
			// Last command outputs to original process file descriptor 1
                	// First command should read from original process file descriptor 0
			// Close pipe ends and redirect output/input
                        //      Note: each command except first/last requires both input/output redirection
			if (numCommands > 1){
                                if (commandIndex == 0){
                                        dup2(pipes[commandIndex][1], STDOUT_FILENO);
                                } else if (commandIndex == numCommands - 1){
                                        dup2(pipes[commandIndex - 1][0], STDIN_FILENO);
                                } else {
                                        dup2(pipes[commandIndex - 1][0], STDIN_FILENO);
                                        dup2(pipes[commandIndex][1], STDOUT_FILENO);
                                }
                        }

			if (numCommands > 1){
                        	for (int j = 0; j < numCommands - 1; j++) {
					close(pipes[j][0]);
                                	close(pipes[j][1]);
                      		}
			}
			
			usleep(250000); // Sleep in miliseconds - 0.25 seconds
					// Note: Just added this to make the output more aligned
					// due to the system resources information that is printed
					
			//////////////// PRINT HISTORY FOR HISTORY COMMAND ///////////////////
			// Check the input command for history
                	if (strcmp(arguments[0], "history") == 0){
                        	// Print out the history buffer
				printf("Write index: %d\n", historyWriteIndex);
				// If buffer is not full, start at index 0 and go til end
                        	// Otherwise start at historyWriteIndex, 
				// move forwards and wrap around until count = historyBufferLength
				int count = 0; // Count represents the number of items printed, loop until all array items were printed
				if (historyBufferLength != MAX_HISTORY_SIZE){ 
					historyReadIndex = 0;
				} else historyReadIndex = historyWriteIndex;
				while (count < historyBufferLength){
					printf("[%d]\t %s", (historyBufferLength - count), history[historyReadIndex]);
					historyReadIndex++;
					if (historyReadIndex > (MAX_HISTORY_SIZE - 1)){
						historyReadIndex = 0;
					}
					count++;
				}
                        	exit(1); // This will make so history command doesn't actually get added to history command list.
					 // Change to exit(0); if wanted to make history command go into command list.
                	}

			//////////////////////// EXECUTE COMMAND ////////////////////
                	// Execute the command with exec
			execvp(arguments[0], arguments); // First arg = command string, 2nd = args array
			perror("exec failed");
                	exit(1);
        	} // CASE: parent past this point
		  // Free allocated memory
		  free(copiedCommand);
		  free(copiedCommand2);
	} // End of piped commands loop
	
	//////////////////////// HANDLE QUIT ////////////////////
	if (breakLoop){ // Break out of main outer loop
		break;
        }

	/////////////////////// PRINT RESOURCE USAGE STATISTICS ///////////////////
	struct rusage usage;
        if (getrusage(RUSAGE_CHILDREN, &usage) == 0) { // On success, 0 is returned, on error -1
        	printf("User CPU time used: %ld.%06ld seconds\n", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec); 
		// time is further stored in timeval struct
                printf("Involuntary context switches: %ld\n", usage.ru_nivcsw);
               	} else {
                perror("get resource usage failed");
               	}

	/////////////// PARENT CLOSE PIPES /////////////////////
	if (numCommands > 1){
		for (int x = 0; x < numCommands - 1; x++){
			close(pipes[x][0]);
			close(pipes[x][1]);
		}
	}
	
	/////////////////////// PARENT WAIT //////////////////////
	// Parent wait for every child and check their status; raise flag if any failed
	for (int w = 0; w < numCommands; w++){
		int status;
		if (waitpid(childPids[w], &status, 0) > 0){
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
				anyChildFailed = 1;
			}
		} else { 
			perror("waitpid"); 
		}
	}

	////////////////////// ADD COMMAND TO HISTORY ////////////////////
	// Check that the command was valid, if so, add it to history
	if (anyChildFailed == 0){
		// Allocate memory for command
		history[historyWriteIndex] = malloc(strlen(fullCommand2) + 1);
		strcpy(history[historyWriteIndex], fullCommand2);
		if (historyBufferLength != MAX_HISTORY_SIZE){
			historyBufferLength++;
		}
		historyWriteIndex++;
		if (historyWriteIndex == MAX_HISTORY_SIZE){ // Reset write index to start if it reaches end
			historyWriteIndex = 0; 
		}
	}	
    } // Outside (while(1) loop
      // Free memory for history array
	for (int i = 0; i < historyBufferLength; i++) {
    		free(history[i]);
	}
    	return 0;
}
