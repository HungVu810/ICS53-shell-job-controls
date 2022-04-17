#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>

#define DEBUG_ENALBED 0

#define MAX_PATH 256 // the current working directory cwd
#define MAX_LINE 80  // the number of characters entered at the prompt
#define MAX_ARGC 80  // the number of argument including the starting command
#define MAX_JOB 5

struct job{
	pid_t pid;  // -1: not avail (not error, faulty fork() is dealt with already)
	int status; // -1: not avail, 0: stopped, 1: bg running, 2: fg running
	unsigned action; // 0: no action, 1: action (could be fg, bg or kill)
	char cmd[MAX_LINE];
	// const char* cmd;
// array contains pid for each running job
} jobs[MAX_JOB] = {[0 ... MAX_JOB - 1] = {-1, -1}};

// if job j is null, wait for any child process
// if hang, keep waiting until a child has exited and reap
void reapJob(struct job *j, unsigned hang){
	int child_status;
	int wret = waitpid(j ? j->pid : -1 , &child_status, hang ? 0: WNOHANG);
# if DEBUG_ENALBED
	if(wret == -1){
		perror("waitpid() error");
	}
# endif
	// skip wret == 0 as WNOHANG is set is child process is not dead yet
	if(wret != -1 && wret){
		// if j is NULL, find it using wret return val
		if(!j){
			for(int i = 0; i < MAX_JOB; i++){
				if(jobs[i].pid == wret){
					j = jobs + i;
					break;
				}
			}
		}
		if(WIFEXITED(child_status)){
			printf("Child process [%u] terminated normally with exit status %i\n", j->pid, WEXITSTATUS(child_status));
		}
		else{
			printf("Child process [%u] terminated abnormally\n", j->pid);
		}
		// reset the job info
		j->pid = -1;
		j->status = -1;
		j->action = 0;
	}
}

void signalHandler(int wsignal){
	switch(wsignal){
		case SIGCHLD:{ // signal sent by a terminated background child process
						 // Wait and reap that child process
						 // WNOHANG to return immediately if no child has exited, to avoid
						 // waiting twice (one by the shell and here) for a terminated
						 // foreground job. This will also work for finished bg job
						 // because the job is already terminated
						 // -1 is for waitting for any child process, in this case will be
						 // the terminated job that send the SIGCHLD signal
#if DEBUG_ENALBED
						printf("caught SIGCHLD\n");
#endif
						reapJob(NULL, 0);
					 } break;
		case SIGINT:{ // intercept signal
						// if there is a running foreground process, send SIGINT to it and
						// reset the job
#if DEBUG_ENALBED
						printf("caught SIGINT\n");
#endif
						for(int i = 0; i < MAX_JOB; i++){
							if(jobs[i].status == 2){
								kill(jobs[i].pid, SIGINT);
								// refrain from refreshing the job info because SIGCHLD will also be sent
								// jobs[i].status = -1;
								// jobs[i].pid = -1;
								break;
							}
						}
					} break;
		case SIGTSTP:{ // stop signal
					   // if there is a running foreground process, send SIGINT/ SIGTSTP to it
#if DEBUG_ENALBED
						printf("caught SIGTSTP\n");
#endif
						for(int i = 0; i < MAX_JOB; i++){
							if(jobs[i].status == 2){
								kill(jobs[i].pid, SIGTSTP);
								jobs[i].status = 0;
							}
						}
					 } break;
	}
	// re-define signal handler to prevent default handling of the signals
	signal(SIGCHLD, signalHandler);
	signal(SIGINT, signalHandler);
	signal(SIGTSTP, signalHandler);
}

// return the lowest available job id (index of jobs)
int lowestAvailJID(){
	for(int i = 0; i < MAX_JOB; i++){
		if(jobs[i].pid == -1) return i;
	}
	// no job id is available
	return -1;
}

int parseTokens(char **argv, char cmdbuffer[MAX_LINE]){
	// important cases: (9 cases in total when swapping around)
	// prompt>...\n
	// prompt>... \n
	// prompt>\n
	size_t i = 0;
	if(scanf("%[^\n]", cmdbuffer)){ // scan the whole line (cmd and its args), only
									// extract the tokens if more than 0 character is
									// scaned
		do{
			argv[i] = strtok( i ? NULL : cmdbuffer, " \t\n");
		} while(argv[i] && ++i < MAX_ARGC);
	}
	// put a NULL ptr at the end of scanned cmd arguments to use execv and execvp (see
	// 'man exec')'
	// argv[i] = NULL;
	// • NO NEED TO APPEND NULL BECAUSE WE ALREADY CLEAR THE ARGV WITH NULL AFTER PARSECMD
	return i;                 // argc
}

int cmdType(int argc, char **argv){
	// -1. invalid cmd
	// 0. jobs builtin
	// 1. quit builtin
	// 2. cd builtin
	// 3. fg builtin
	// 4. bg builin
	// 5. kill builtin
	// 6. fg general cmd
	// 7. bg general cmd
	if(argv[0]){
		if(!strcmp(*argv, "jobs")){ // builtin commands
			if(argc == 1) return 0;
		}
		else if(!strcmp(*argv, "quit")){
			if(argc == 1) return 1;
		}
		else if(!strcmp(*argv, "cd")){
			// check if path is valid
			if(argc == 2 && fopen(argv[1], "r")) return 2;
		}
		else if((!strcmp(*argv, "fg") || !strcmp(*argv, "bg") || !strcmp(*argv, "kill")) && argc == 2){
				// check if job_id or pid is valid
				int x_id;
				if(*argv[1] == '%'){
					x_id = atoi(argv[1] + 1);
				}
				else{
					x_id = atoi(argv[1]);
				}
				// proceed if atoi sucess
				if(x_id){
					for(int i = 0; i < MAX_JOB; i++){
						// jod id start with '%' and 1 <= # <= MAX_JOB, otherwise check for pid
						if(jobs[i].pid != -1 && ((x_id - 1 == i && argv[1][0] == '%') || x_id == jobs[i].pid)){
							jobs[i].action = 1;
#if DEBUG_ENALBED
							printf("job [%u] is marked for \"%s\"\n", jobs[i].pid, *argv);
#endif
							if(!strcmp(*argv, "fg")) return 3;
							else if(!strcmp(*argv, "bg")) return 4;
							else return 5;
						}
					}
				}
				else{
					printf("Invalid job id or process id\n");
				}
		}
		else{ // possibly general commands, if the command is invalid the processGeneralCmd
			  // will handle that
			// check if '&' is at the end of the command for a background job
			if(argv[argc-1][0] == '&') return 7;
			// otherwise foreground
			else return 6;
		}
	}
	return -1;
}

int processBuiltInCmd(int command_type, int argc, char **argv){
	// All built-in cmds run in the foreground.
	// 0. quit
	// 1. failed or success
	switch(command_type){
		case 0: { // job builtin
					for(int i = 0; i < MAX_JOB; i++){
						if(jobs[i].pid != -1){
							const char *status;
							switch(jobs[i].status){
								 // -1 ?
								 // 2 ? No need to because the job must be dealth with
								 // before the current process can run
								case 0: status = "Stopped"; break;
								case 1: status = "Running"; break; // background running
								default: break;
							}
							printf("[%u] (%u) %s %s\n", i + 1, jobs[i].pid, status, jobs[i].cmd);
						}
					}
				} break;
		case 1: { // quit builtin
					// reap child processes in jobs
					for(int i = 0; i < MAX_JOB; i++){
						// terminate, send SIGKILL, SIGINT or SIGSTP to child ?
						// don't wait and kill all the child processes
						if(jobs[i].pid != -1){
							kill(jobs[i].pid, SIGINT);
						}
					}
					return 0;
				} break;
		case 2: { // cd builtin
					// get the curent working directory, the last char is reserved for \n
					// change the $PWD to the expanded path specified 

					// char cwd[MAX_PATH];
					// if(getcwd(cwd, MAX_PATH - 1)){
					// 	printf("Can't get the current working directory");
					// }
					if(chdir(argv[1])){
						printf("Failed to change to the specified file or directory\n");
					}
					// a string contains the current working directory ?
				} break;
		case 3: // -- DROP DOWN -- fg builtin, bring a bg running or stopped job to fg
		case 4: // -- DROP DOWN -- bg builin, bring a stopped job to the bg
		case 5: {// kill builtin
					struct job *j = NULL;
					int child_status;
					for(int i = 0; i < MAX_JOB; i++){
						if(jobs[i].action){
							j = jobs + i;
						}
						// no more action needed
						j->action = 0;
					}
					// continue stopped process
					if(command_type == 3 || command_type == 4){
						if(j->status == 0){ // stopped job, otherwise running background job
#if DEBUG_ENALBED
							printf("SIGCONT sent to job [%u]\n", j->pid);
#endif
							kill(j->pid, SIGCONT); // send a continue signal to the stopped job
						}
					}
					if(command_type == 3){ // fg
						// reassign job variable
						j->status = 2;
						// wait to reap the job when finished
						reapJob(j, 1);
					}
					else if(command_type == 4){ // bg
						// rassign job variable and don't wait for the job to finish. Let
						// the signal handler handles the SIGCHLD
						// reassign job variable
						j->status = 1;
					}
					else { // kill
						// send SIGINT to the child process, the signal handler will
						// handle the reaping of the process by handling SIGCHLD
#if DEBUG_ENALBED
							printf("SIGINT sent to job [%u]\n", j->pid);
#endif
						kill(j->pid, SIGINT);
						// reset the job info
						j->pid = -1;
						j->status = -1;
					}
				} break;
	}
	return 1;
}

void processGeneralCmd(unsigned is_background, int argc, char **argv, char cmdbuffer[MAX_LINE]){
	// Issued general cmds (must be within the current directory) can either be running in the background or the foreground
	// TODO: need to return ?
	// 0. failed
	// 1. sucess
	int jid = lowestAvailJID();
	if(jid == -1){
		printf("No Job ID left to be used (max %u job(s))\n", MAX_JOB);
		// return 0;
	}
	else{
		// assign job info immediately before the child send SIGCHLD. Prioritize trival
		// member variables first before assigning pid
		jobs[jid].status = is_background ? 1 : 2;
		strcpy(jobs[jid].cmd, cmdbuffer);
		jobs[jid].pid = fork();
		if(jobs[jid].pid == -1){
			perror("fork() error");
			// return 0;
		}
		else if(!jobs[jid].pid){ // child process
			// Execute, argv[0] is the command (executable binary or script), argv is the
			// argument for $0, $1, $2,...

			// Both execv and execvp can works with relative/ absolute path, the only difference is when only the file name is specified, execv will ONLY search for the local directory while the execvp will ONLY search in the $PATH
			// If execv failed, try execute with $PATH via execvp
#if DEBUG_ENALBED
			sleep(2);
#endif
			if(execv(argv[0], argv) == -1 && execvp(argv[0], argv) == -1){
				// printf("Execv() failed\n");
				// printf("Invalid command\n");
				perror("exec error");
				// exit immediately or the child process will continue where the parent left off
				exit(EXIT_FAILURE);
			}
		}
		else{ // current process
			// background child job forked, do not wait, only handle the SIGCHLD
			// signal to reap it
			if(!is_background){ // foreground child job forked
				reapJob(jobs + jid, 1);
			}
		}
	}
	// return 1;
}

int parseCmd(int argc, char **argv, char cmdbuffer[MAX_LINE]){
	// 0. quit
	// 1. failed or success (outputs are done in dedicated functions above)
	// check if there are command to be parsed
	int command_type = cmdType(argc, argv);
	switch(command_type){
		case -1: break; // invalid cmd
		case 0: // -- DROP DOWN -- jobs builtin
		case 1: // -- DROP DOWN -- quit builtin
		case 2: // -- DROP DOWN -- cd builtin
		case 3: // -- DROP DOWN -- fg builtin
		case 4: // -- DROP DOWN -- bg builin
		case 5: { // kill builtin
					// either return 1 or 0 (quit)
					return processBuiltInCmd(command_type, argc, argv);
				} break;
		case 6: // -- DROP DOWN -- fg general cmd
		case 7: { // bg general cmd
					processGeneralCmd(command_type - 6, argc, argv, cmdbuffer);
				} break;
	}
	return 1;
}

int main(){
	// TODO: redirection > >>, <

	// define signal handler
	signal(SIGCHLD, signalHandler);
	signal(SIGINT, signalHandler);
	signal(SIGTSTP, signalHandler);

	char *argv[MAX_ARGC + 1] = { [0 ... MAX_ARGC] = NULL };
	// 255 char max, the last is for \0
	char cmdbuffer[MAX_LINE];
	int quit = 0;
	do{
		// only print prompt if no foreground job is running
		int fg_job_running = 0;
		for(int i = 0; i < MAX_JOB; i++){
			if(jobs[i].status == 2){
				fg_job_running = 1;
				break;
			}
		}
		if(!fg_job_running){
			printf("prompt> ");
			int argc = parseTokens(argv, cmdbuffer);
			if(!parseCmd(argc, argv, cmdbuffer)){
				quit = 1;
			}
			while(getchar() != '\n'); // clear input buffer to print the next prompt
			// clear argv for the next cmd
			for(int i = 0; i < argc; i++){
				argv[i] = NULL;
			}
		}
	} while(!quit);
	// if(argc){ // only proceed if a command is given
	// 	switch(parseCmd(argc, argv, cmdbuffer)){
	// 		case -2: printf("All Job IDs are in used.\n"); break; // no job id avaialble
	// 		case -1: printf("Command not found.\n"); break; // invalid command
	// 		case 0: quit = 1; break;  // quit, TODO: REAP ALL CHILD PROCESSES
	// 		case 1: break;  // cmd parsed and processed sucessfully
	// 	}
	// }
	return 0;
}

// SPECIFICATION
// gcc -o -std=c99 -stdc11
// MaxLine: 80, MaxArgc: 80, MaxJob: 5 
// Use both execvp() and execv() to allow either case. 
// execvp() :  Linux commands {ls, cat, sort, ./hellp, ./slp}. 
// execv() :  Linux commands {/bin/ls, /bin/cat, /bin/sort, hello, slp}

// command [arg1] [arg2] ... [argn]'\n'. Whitespaces or tabs as delimeter
// fork, redirect stdout to here (or use the default stdout ?) and exec
// support built-int cmds: jobs, bg, fb, kill, cd and quit. Executed by the shell directly
// support all general commands (must be executable binaries), ASSUME THAT THE GENERAL COMMAND MUST BE IN THE CURRENT DIRECTORY (ie: prompt> some_binary). Executed via fork and execute (child process)
// I/O redirection (after fork and before exec)

// const char* prompt = "prompt> ";
// parseTokens(tokens);
// const char *tokens[MAX_ARGC] = { [0 ... MAX_ARGC - 1] = NULL };
// const char *prompt = "prompt> hello arg1 arg2	arg3";
// char *buffer = (char*)malloc(256);
// size_t i = 0;
// do{
// 	tokens[i] = strtok( i ? NULL : strcpy(buffer, prompt), " \t");
// } while(tokens[i] && ++i < MAX_ARGC);
// for(int i = 0; i < MAX_ARGC; i++){
// 	printf("%s", tokens[i]);
// }
// free(buffer);
