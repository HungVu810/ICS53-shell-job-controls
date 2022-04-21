#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h> // file descriptor redirection

#define DEBUG_ENALBED 0

#define MAX_PATH 256 // the current working directory cwd
#define MAX_LINE 80  // the number of characters entered at the prompt
#define MAX_ARGC 80  // the number of argument including the starting command
#define MAX_JOB 5
// #define currentpgid getpgid(getpid())

// can't do tcsetpgrp because ^z must always go through the shell to update jobs' info
struct job{
	int pid;
	/* -1: unknown */
	/* 0: bg */
	/* 1: stopped */
	/* 2: foreground */
	int status;
	/* 0: not terminated */
	/* 1: terminated */
	int terminated;
	char cmd[MAX_LINE];
} jobs[MAX_JOB] = { [0 ... MAX_JOB - 1] = { -1, -1, 1 } };
char *argv[MAX_ARGC + 1] = { [0 ... MAX_ARGC] = NULL };
// not altered by strstok
char cmdbuffer_unaltered[MAX_LINE] = { [0 ... MAX_LINE - 1] = 0 };
char cmdbuffer[MAX_LINE] = { [0 ... MAX_LINE - 1] = 0 };
/* int fd; // fd of he current terminal */

// forward declare
void SIGCHLDhandler(int signal);
void SIGINThandler(int signal);
void SIGTSTPhandler(int signal);

// check if there is a foreground job, return jid is true, -1 otherwise
int getfjid(){
	for(int i = 0; i < MAX_JOB; i++){
		if(jobs[i].status == 2) return i;
	}
	return -1;
}

// convert pid to jid [0, MAX_JOB), return -1 if failed
// int pid, not unsigned pid because atoi can return negative number
int pidtojid(int pid){
	// pid should > 0 otherwise an error because atoi return 0 on error
	if(pid > 0){
		for(int i = 0; i < MAX_JOB; i++){
			if(jobs[i].pid == pid){
#if DEBUG_ENALBED
				printf("pid [%u] converted to jid [%i]\n", pid, i);
#endif
				return i;
			}
		}
	}
#if DEBUG_ENALBED
	printf("failed to convert pid [%u]\n", pid);
#endif
	return -1;
}

// return jid from argv if issued builtin cmd such as 'fg', 'bg' or 'kill',
// otherwise return -1 if not avail
int getcmdjid(){
	if(argv[1] != NULL){
		if(*argv[1] == '%'){ // jid
			int jid = atoi(argv[1] + 1) - 1;
			if(0 <= jid && jid < MAX_JOB && jobs[jid].pid != -1){
#if DEBUG_ENALBED
				printf("job id [jid:%i, pid:%i] returned\n", jid, jobs[jid].pid);
#endif
				return jid;
			}
		}
		else{ // pid
			int jid = pidtojid(atoi(argv[1]));
			if(jid != -1){
#if DEBUG_ENALBED
				printf("job id [%i] returned\n", jid);
#endif
				return jid;
			}
		}
	}
#if DEBUG_ENALBED
	printf("failed to find cmdpid\n");
#endif
	return -1;
}

// return the lowest available job id (index of jobs)
int lowestAvailJID(){
	for(int i = 0; i < MAX_JOB; i++){
		if(jobs[i].pid == -1) return i;
	}
	// no job id is available
	return -1;
}

// reset job and return foreground group of the current terminal to current process
void resetjob(unsigned jid){
	if(0 <= jid && jid < MAX_JOB){
#if DEBUG_ENALBED
		printf("jid [%u] reseted\n", jid);
#endif
		jobs[jid].pid = -1;
		jobs[jid].status = -1;
		jobs[jid].terminated = 1;
		strcpy(jobs[jid].cmd, "");
	}
#if DEBUG_ENALBED
	else printf("can't reset jid [%u]\n", jid);
#endif
	// return the foreground group to the current process
	// tcsetpgrp(fd, currentpgid);
}

/* Create a new process group id and set the foreground group to that pgid. Must be called
 * by both the parent and the child to set the fgpid properly regardless of which process
 * is scheduled first */
/* Return 0 if success */
/* Return -1 if fail */
	// int newPgidSetsFgroup(int fd, unsigned pid){
	// 	// if pid = 0 (child) then setpid(0, 0)
	// 	if(setpgid(pid, pid) != -1){
	// 		// pid = 0 (child), can't be use
	// 		if(!pid) return 0;
	// 		// pid = child pid, use to set the fg pid
	// 		else if(tcsetpgrp(fd, pid) != -1) return 0;
	// 		perror(NULL);
	// 	}
	// 	perror(NULL);
	// 	return -1;
	// }

// =========================== SUPPORT FUNCTIONS =========================== 

void SIGCHLDhandler(int signal){
	// child terminated (SIGINT, or returned from exec's program) or stopped (SIGTSTP)
#if DEBUG_ENALBED
	printf("caught SIGCHLD\n");
#endif
	int stat_loc;
	// wait for a process to stop (not reap) or terminate (reap) and report the status
	int pid = waitpid(-1, &stat_loc, WNOHANG);
#if DEBUG_ENALBED
	printf("sigchld pid: %i\n", pid);
#endif
	if(pid && pid != -1){
		int jid = pidtojid(pid);
		// this conditional statment is trival when processBuiltInQuit called succesfully returned
		// otherwise, 
		if(jid != -1 && jobs[jid].terminated) resetjob(jid);
	}
#if DEBUG_ENALBED
	else if(pid == -1) perror(NULL);
	// pid = 0
	else printf("no state change detected\n");
#endif
	/* printf("\n"); // print a line feed to push prompt> into newline */
}

void SIGINThandler(int signal){
	// SIGINT is fowarded to every jobs the shell created. Sent SIGINT only to the
	// foreground process, whose pid == pgid != parent process pid
#if DEBUG_ENALBED
	printf("caught SIGINT\n");
#endif
	// if there is a foreground job
	int fjid = getfjid();
	if(fjid != -1){
	// if(tcgetpgrp(fd) != currentpgid){
		// sent SIGINT to all processes in the foreground
		// killpg(tcgetpgrp(fd), SIGINT);
		jobs[fjid].terminated = 1;
		kill(jobs[fjid].pid, SIGINT);
		// resetjob(tcgetpgrp(fd));
		/* resetjob(fjid); */
	}
#if DEBUG_ENALBED
	else printf("No foreground running job atm\n");
#endif
	printf("\n"); // print a line feed to push prompt> into newline
}

void SIGTSTPhandler(int signal){ // stop signal
#if DEBUG_ENALBED
	printf("caught SIGTSTP\n");
#endif
	// if there is a foreground job
	int fjid = getfjid();
	if(fjid != -1){
	// if(tcgetpgrp(fd) != currentpgid){
		// sent SIGINT to all processes in the foreground
		// if(killpg(tcgetpgrp(fd), SIGTSTP) != -1){
			// send sigtstp, sigchld will be handled because
			// of the child state change but will be ignored
			jobs[fjid].terminated = 0;
			jobs[fjid].status = 1;
			kill(jobs[fjid].pid, SIGTSTP);
#if DEBUG_ENALBED
			printf("jobs [%u] stopped\n", jobs[fjid].pid);
#endif
		// }
		// perror(NULL);
	}
#if DEBUG_ENALBED
	else printf("No foreground running job atm\n");
#endif
	printf("\n"); // print a line feed to push prompt> into newline
}

// wait for foreground job jid to finish. Also handle special cases such as SIGTSTP
void waitfgjob(int jid){
	// if(newPgidSetsFgroup(fd, jobs[jid].pid) != -1){
	// set the pgid of the child to its pid instead of keeping the inherinted
	// process gid to prevent reciveing forground signal from the current process (tcgetpgrp == currentpgid)
	jobs[jid].status = 2;
	jobs[jid].terminated = 1;
	setpgid(jobs[jid].pid, jobs[jid].pid);
	int stat_loc;
#if DEBUG_ENABLED
	printf("waiting to reap child process [%u]\n", jobs[jid].pid);
#endif
	int wpid = waitpid(jobs[jid].pid, &stat_loc, 0);
	// wpid can't be 0 because option = 0
	if(wpid != -1 && wpid){
		// Make sure that the job is terminated in case the terminating status is changed
		// when waiting (due to ctrl-z)
		// Can't termiate in SIGCHLDhandler because waitpid in that function will return 0
		// due to WNOHANG and because the job is already reaped by waitpid in this
		// function

		// TODO: DOUBLE PROMPT> KILL or CTRL-z FOREGROUND PROCESS, disable disfunction diable that ?
		if(jobs[jid].terminated) resetjob(jid);

#if DEBUG_ENABLED
		printf("child reaped\n");
		// if(tcsetpgrp(fd, currentpgid) == -1) perror(NULL);
		if(WIFEXITED(stat_loc)){
			printf("Child process [%u] terminated normally with exit status %i\n", jobs[jid].pid, WEXITSTATUS(stat_loc));
		}
		else{
			printf("Child process [%u] terminated abnormally\n", jobs[jid].pid);
		}
#endif
	}
	// wpid == -1
#if DEBUG_ENALBED
	else perror(NULL);
#endif
	// }
	// else perror(NULL);
#if DEBUG_ENABLED
	else printf("invalid jid [%i] (waitfgjob)", jid);
#endif
}

void processBuiltInJobs(){
	for(int i = 0; i < MAX_JOB; i++){
		if(jobs[i].pid != -1){
			const char *status;
			switch(jobs[i].status){
				case 0: status = "Running"; break;
				case 1: status = "Stopped"; break; // background running
				default: status = "Unknown"; break;
			}
			printf("[%u] (%u) %s %s\n", i + 1, jobs[i].pid, status, jobs[i].cmd);
		}
	}
}

void processBuiltInQuit(){
	// reap child processes in jobs
	for(int i = 0; i < MAX_JOB; i++){
		if(jobs[i].pid != -1){
			// trival, the program would exit and 'jobs' is not going to be used
			/* jobs[i].terminated = 1; */
			kill(jobs[i].pid, SIGINT);
		}
	}
}

void processBuiltInCd(){
	if(chdir(argv[1]) == -1){
#if DEBUG_ENABLED
		perror(NULL);
#endif
	}
}

// print invalid command if applicable (return 0 if invalid, 1 if valid)
void processBuiltInFg(int jid){
	// send continue signal, ignored if already running
	kill(jobs[jid].pid, SIGCONT);
	// TODO: could it possible that tcgetpgrp() != currentpgid
	// newPgidSetsFgroup(fd, jobs[jid].pid);
	waitfgjob(jid);
#if DEBUG_ENALBED
	for(int i = 0; i < MAX_JOB; i++){
		if(i == 0) printf("current pgid: %i\n", getpgid(getpid()));
		printf("job [jid:%i] [pid:%i] [pgid:%i] [stat:%i] [term:%i] [%s]\n", i + 1, jobs[i].pid, getpgid(jobs[i].pid), jobs[i].status, jobs[i].terminated, jobs[i].cmd);
	}
#endif
}

void processBuiltInBg(int jid){
	// send continue signal
	jobs[jid].status = 0;
	jobs[jid].terminated = 1;
	kill(jobs[jid].pid, SIGCONT);
#if DEBUG_ENALBED
	for(int i = 0; i < MAX_JOB; i++){
		if(i == 0) printf("current pgid: %i\n", getpgid(getpid()));
		printf("job [jid:%i] [pid:%i] [pgid:%i] [stat:%i] [term:%i] [%s]\n", i + 1, jobs[i].pid, getpgid(jobs[i].pid), jobs[i].status, jobs[i].terminated, jobs[i].cmd);
	}
#endif
}

void processBuiltInKill(int jid){
	jobs[jid].terminated = 1;
	// TODO: some child can ignore sigint ? change to sigkill
	kill(jobs[jid].pid, SIGKILL);
	resetjob(jid);
#if DEBUG_ENALBED
	for(int i = 0; i < MAX_JOB; i++){
		if(i == 0) printf("current pgid: %i\n", getpgid(getpid()));
		printf("job [jid:%i] [pid:%i] [pgid:%i] [stat:%i] [term:%i] [%s]\n", i + 1, jobs[i].pid, getpgid(jobs[i].pid), jobs[i].status, jobs[i].terminated, jobs[i].cmd);
	}
#endif
}

int processGeneralFg(){
	int jid = lowestAvailJID();
	if(jid == -1){
		printf("No Job ID left to be used (max %u job(s))\n", MAX_JOB);
	}
	else{
		strcpy(jobs[jid].cmd, cmdbuffer_unaltered);
		int pid = fork();
		if(pid == -1){
#if DEBUG_ENABLED
			perror(NULL);
#endif
		}
		else if(!pid){ // child process
			// if(newPgidSetsFgroup(fd, jobs[jid].pid) != -1){
				// set the pgid of the child to itself instead of keeping the inherinted
				// process gid to prevent reciveing forground signal from the current process (tcgetpgrp == currentpgid)
				setpgid(0, 0);
				if(execv(argv[0], argv) == -1 && execvp(argv[0], argv) == -1){
					perror("Unknown or invalid command");
					exit(EXIT_FAILURE);
				}
			// }
		}
		else{ // current process
			jobs[jid].pid = pid;
			waitfgjob(jid);
		}
		return 1;
	}
#if DEBUG_ENALBED
	for(int i = 0; i < MAX_JOB; i++){
		if(i == 0) printf("current pgid: %i\n", getpgid(getpid()));
		printf("job [jid:%i] [pid:%i] [pgid:%i] [stat:%i] [term:%i] [%s]\n", i + 1, jobs[i].pid, getpgid(jobs[i].pid), jobs[i].status, jobs[i].terminated, jobs[i].cmd);
	}
#endif
	return 0;
}

int processGeneralBg(){
	int jid = lowestAvailJID();
#if DEBUG_ENABLED
	printf("bg job\n");
#endif
	if(jid == -1){
		printf("No Job ID left to be used (max %u job(s))\n", MAX_JOB);
	}
	else{
		strcpy(jobs[jid].cmd, cmdbuffer_unaltered);
		jobs[jid].status = 0;
		jobs[jid].terminated = 1;
		jobs[jid].pid = fork();
		if(jobs[jid].pid == -1){
#if DEBUG_ENABLED
			perror(NULL);
#endif
		}
		else if(!jobs[jid].pid){ // child process
			// set the pgid of the child to itself instead of keeping the inherinted
			// process gid to prevent reciveing forground signal from the current process (tcgetpgrp == currentpgid)
			setpgid(0, 0);
			if(execv(argv[0], argv) == -1 && execvp(argv[0], argv) == -1){
#if DEBUG_ENABLED
				perror(NULL);
#endif
				exit(EXIT_FAILURE);
			}
		}
		else{ // parent process
			// set the pgid of the child to itself instead of keeping the inherinted
			// process gid to prevent reciveing forground signal from the current process (tcgetpgrp == currentpgid)
			setpgid(jobs[jid].pid, jobs[jid].pid);
		}
		// dont wait for the child process, only handle its signal
#if DEBUG_ENALBED
	for(int i = 0; i < MAX_JOB; i++){
		if(i == 0) printf("current pgid: %i\n", getpgid(getpid()));
		printf("job [jid:%i] [pid:%i] [pgid:%i] [stat:%i] [term:%i] [%s]\n", i + 1, jobs[i].pid, getpgid(jobs[i].pid), jobs[i].status, jobs[i].terminated, jobs[i].cmd);
	}
#endif
		return 1;
	}
	return 0;
}

void redirectIO(int argc){
	// argv is empty or not is checked at the beginning of parseCmd
	mode_t mode = S_IRWXU | S_IRWXG | S_IRWXO;
	// the lowest i such that *argv[i] == <, >, or >>
	int redirect_start = -1;
	for(int i = 0; i < argc; i++){
		if(!strcmp(argv[i], ">")){
			// argv[i - 1] > argv[i + 1], argv[i - 1] is a program and argv[i + 1] is a file
			if(i + 1 < argc && argv[i + 1]){
				/* Output redirected to argv[i + 1] (Create or Write) */
				int outFileID = open(argv[i + 1], O_CREAT|O_WRONLY|O_TRUNC, mode);
				dup2(outFileID, STDOUT_FILENO);
				// close unused fd
				close(outFileID);
				if(redirect_start == -1) redirect_start = i;
			}
		}
		else if(!strcmp(argv[i], "<")){
			// argv[i - 1] < argv[i + 1], argv[i - 1] is a program and argv[i + 1] is a file
			if(i + 1 < argc && argv[i + 1]){
				/* Input redirected to argv[i + 1] (Read) */
				int inFileID = open(argv[i + 1], O_RDONLY, mode);
				dup2(inFileID, STDIN_FILENO);
				// close unused fd
				close(inFileID);
				if(redirect_start == -1) redirect_start = i;
			}
		}
		else if(!strcmp(argv[i], ">>")){
			// argv[i - 1] >> argv[i + 1], argv[i - 1] is a program and argv[i + 1] is a file
			if(i + 1 < argc && argv[i + 1]){
				/* Output appended to argv[i + 1] (Create or Append) */
				// add write option, and remove truncate for appending to file to work properly
				int outFileID = open(argv[i + 1], O_CREAT|O_WRONLY|O_APPEND, mode);
				dup2(outFileID, STDOUT_FILENO);
				// close unused fd
				close(outFileID);
				if(redirect_start == -1) redirect_start = i;
			}
		}
	}
	if(redirect_start != -1){
		// only parse the cmd and its argument and the argument where the redirection symbol starts
		// > test.txt echo "hello" works in bash but this is a simple shell
		argv[redirect_start] = NULL;
	}
}

int parseCmd(int argc){
	if(*argv){
		redirectIO(argc); // redirect stdin (<), stdout (>) or append (>>)
		if(!strcmp(*argv, "jobs")){ // builtin commands
			if(argc == 1) processBuiltInJobs();
			else return 0;
		}
		else if(!strcmp(*argv, "quit")){
			if(argc == 1){
				processBuiltInQuit();
				return -1;
			} 
			else return 0;
		}
		else if(!strcmp(*argv, "cd")){
			// check if path is valid
			if(argc == 2) processBuiltInCd();
			else return 0;
		}
		else if(!strcmp(*argv, "fg")){
			int jid = getcmdjid();
			if(jid != -1 && (jobs[jid].status == 0 || jobs[jid].status == 1)) processBuiltInFg(jid);
			else return 0;
		}
		else if(!strcmp(*argv, "bg")){
			int jid = getcmdjid();
			if(jid != -1 && jobs[jid].status == 1) processBuiltInBg(jid);
			else return 0;
		}
		else if(!strcmp(*argv, "kill")){
			int jid = getcmdjid();
			if(jid != -1) processBuiltInKill(jid);
			else return 0;
		}
		else if(argv[argc-1][0] == '&'){ // possible general background
			// don't include the argv[i] = '&' since it can be an invalid argument (such
			// as sleep 500 &)
			argv[argc-1] = NULL;
			return processGeneralBg();
		}
		else {
			return processGeneralFg();
		}
	}
	return 1;
}

// ps -a | grep hw2 | cut -d ' ' -f 2

// ctrl-c, ctrl-z will skip scanf, num_matched_char == -1
// num_matched_char == 0 means entered '\n' into the prompt, otherwise num_matched_char > 0
void cleanupIO(int argc, int num_matched_char){
	// case 1: argc 0 and no value in input stream (ctrl-c, ctr-z)
	// case 2: argc 0 and '\n' in input stream (press enter without cmd)
	// num_matched_char can be EOF (see parseTokens)
	if(num_matched_char != -1 && num_matched_char != EOF) while(getchar() != '\n');
	for(int i = 0; i < MAX_LINE; i++){
		if(cmdbuffer[i]) cmdbuffer[i] = 0;
		if(cmdbuffer_unaltered[i]) cmdbuffer_unaltered[i] = 0;
	}
	for(int i = 0; i < argc; i++){
		argv[i] = NULL;
	}
}

/* parse token, return -1 if failed, argc if success */
// NOTE: CTRL-C, CTRL-Z WILL SKIP SCANF (num_matched_char = -1)
int parseTokens(int *num_matched_char){
	// prompt_printed is used to prevent prompt> to print twice incase of ctrl-z or kill in some situation
	static int prompt_printed = 0;
	size_t i = 0;
	// if there is no foreground job
	if(getfjid() == -1){
	// if(tcgetpgrp(fd) == currentpgid){
		if(!prompt_printed){
			printf("prompt> ");
			prompt_printed = 1;
		}
		if((*num_matched_char = scanf("%[^\n]", cmdbuffer_unaltered)) != EOF){
			// num_matched_char == 0 means entered '\n' into the prompt, otherwise a
			// potential command
			if(num_matched_char){
				strcpy(cmdbuffer, cmdbuffer_unaltered);
				do{
					argv[i] = strtok( i ? NULL : cmdbuffer, " \t\n");
				} while(argv[i] && ++i < MAX_ARGC);
			}
		}
		else{
#if DEBUG_ENABLED
			perror(NULL);
#endif
			return -2;
		}
#if DEBUG_ENALBED
		printf("input cmd: %s\n", cmdbuffer_unaltered);
#endif
	}
#if DEBUG_ENALBED
	printf("there is a running foreground job, not parsing token\n");
#endif
	// during prompt> scanf, recieved signal will pop function this from the stack during scanf waiting for input so it is unlikely that it would reach here, only when scanf is sucess then prompt_printed is enabled.
	prompt_printed = 0;
	return i; // argc
}

int main(){
	// tty fd
	/* FILE *f = fopen(ctermid(NULL), "r"); */
	/* fd = fileno(f); */
	/* if(fd != -1){ */
		int quit = 0;
		// original copy of the stdin and stdout fd
		int stdin_cpy = dup(STDIN_FILENO);
		int stdout_cpy = dup(STDOUT_FILENO);
		do{
			// set the pgid of each spawned process to its own pid so the signal does not
			// propagated to the child pid
			/* signal(SIGTTOU, SIG_IGN); */
			signal(SIGCHLD, SIGCHLDhandler);
			signal(SIGINT, SIGINThandler);
			signal(SIGTSTP, SIGTSTPhandler);
			int num_matched_char = -1;
			int argc = parseTokens(&num_matched_char);
			if(argc){
				int parse_ret = parseCmd(argc);
				switch(parse_ret){
					case -2: printf("Failed to parse command\n"); break;
					case -1: quit = 1; break;
					case 0: printf("Invalid command.\n"); break;
					default: break; // failed or pass but cmd parsed
				}
			}
			cleanupIO(argc, num_matched_char);
			// restore input output to stdin and stdout in case of redirectIO is called
			dup2(stdin_cpy, STDIN_FILENO);
			dup2(stdout_cpy, STDOUT_FILENO);
		} while(!quit);
	/* } */
#if DEBUG_ENABLED
	else perror(NULL);
#endif
	return 0;
}

// #define DEBUG_ENALBED 1

// #define MAX_PATH 256 // the current working directory cwd
// #define MAX_LINE 80  // the number of characters entered at the prompt
// #define MAX_ARGC 80  // the number of argument including the starting command
// #define MAX_JOB 5

// struct job{
// 	pid_t pid;  // -1: not avail (not error, faulty fork() is dealt with already)
// 	int status; // -1: not avail, 0: stopped, 1: bg running, 2: fg running
// 	unsigned action; // 0: no action, 1: action (could be fg, bg or kill)
// 	char cmd[MAX_LINE];
// 	// const char* cmd;
// // array contains pid for each running job
// } jobs[MAX_JOB] = {[0 ... MAX_JOB - 1] = {-1, -1}};

// // if job j is null, wait for any child process
// // if hang, keep waiting until a child has exited and reap
// void reapJob(struct job *j, unsigned hang){
// 	int child_status;
// 	int wret = waitpid(j ? j->pid : -1 , &child_status, hang ? 0: WNOHANG);
// # if DEBUG_ENALBED
// 	if(wret == -1){
// 		perror("• waitpid() error");
// 	}
// # endif
// 	// skip wret == 0 as WNOHANG is set is child process is not dead yet
// 	if(wret != -1 && wret){
// #if DEBUG_ENALBED
// 		printf("• request made to reap pid [%i]\n", wret);
// 		for(int i = 0; i < MAX_JOB; i++){
// 			printf("• jid:%i, pid:%u, status:%i, action:%u, cmd:%s\n", i + 1, jobs[i].pid, jobs[i].status, jobs[i].action, jobs[i].cmd);
// 		}
// # endif
// 		// if j is NULL, find it using wret return val
// 		if(!j){
// 			for(int i = 0; i < MAX_JOB; i++){
// 				if(jobs[i].pid == wret){
// 					j = jobs + i;
// 					break;
// 				}
// 			}
// 		}
// # if DEBUG_ENALBED
// 		if(WIFEXITED(child_status)){
// 			printf("Child process [%u] terminated normally with exit status %i\n", j->pid, WEXITSTATUS(child_status));
// 		}
// 		else{
// 			printf("Child process [%u] terminated abnormally\n", j->pid);
// 		}
// # endif
// 		// clear input buffer if the reaped job is foreground, in case the user typed
// 		// while the job is running
// 		// if(j->status == 2){
// 		// while(getchar() != '\n');
// 		// }
// 		// reset the job info
// 		j->pid = -1;
// 		j->status = -1;
// 		j->action = 0;
// 		for(int i = 0; i < MAX_LINE; i++){
// 			j->cmd[i] = 0;
// 		}
// 	}
// }

// void signalHandler(int wsignal){
// 	switch(wsignal){
// 		case SIGCHLD:{ // signal sent by a terminated background child process
// 						 // Wait and reap that child process
// 						 // WNOHANG to return immediately if no child has exited, to avoid
// 						 // waiting twice (one by the shell and here) for a terminated
// 						 // foreground job. This will also work for finished bg job
// 						 // because the job is already terminated
// 						 // -1 is for waitting for any child process, in this case will be
// 						 // the terminated job that send the SIGCHLD signal
// #if DEBUG_ENALBED
// 						printf("• caught SIGCHLD\n");
// #endif
// 						reapJob(NULL, 0);
// 					 } break;
// 		case SIGINT:{ // intercept signal
// 						// if there is a running foreground process, send SIGINT to it and
// 						// reset the job
// #if DEBUG_ENALBED
// 						printf("• caught SIGINT\n");
// #endif
// 						for(int i = 0; i < MAX_JOB; i++){
// 							if(jobs[i].status == 2){
// 								kill(jobs[i].pid, SIGINT);
// 								// reset the job info
// 								// jobs[i].pid = -1;
// 								// jobs[i].status = -1;
// 								// jobs[i].action = 0;
// 								// for(int j = 0; j < MAX_LINE; j++){
// 								// 	jobs[i].cmd[j] = 0;
// 								// }
// 								break;
// 							}
// 						}
// 					} break;
// 		case SIGTSTP:{ // stop signal
// 					   // if there is a running foreground process, send SIGINT/ SIGTSTP to it
// #if DEBUG_ENALBED
// 						printf("• caught SIGTSTP\n");
// #endif
// 						for(int i = 0; i < MAX_JOB; i++){
// 							if(jobs[i].status == 2){
// 								kill(jobs[i].pid, SIGTSTP);
// 								jobs[i].status = 0;
// 							}
// 						}
// 					 } break;
// 	}
// 	// re-define signal handler to prevent default handling of the signals
// 	signal(SIGCHLD, signalHandler);
// 	signal(SIGINT, signalHandler);
// 	signal(SIGTSTP, signalHandler);
// }

// // return the lowest available job id (index of jobs)
// int lowestAvailJID(){
// 	for(int i = 0; i < MAX_JOB; i++){
// 		if(jobs[i].pid == -1) return i;
// 	}
// 	// no job id is available
// 	return -1;
// }

// int parseTokens(char **argv, char cmdbuffer[MAX_LINE]){
// 	// important cases: (9 cases in total when swapping around)
// 	// prompt>...\n
// 	// prompt>... \n
// 	// prompt>\n
// 	size_t i = 0;
// 	int fg_job_running = 0;
// #if DEBUG_ENALBED
// 	printf("• waiting cmd\n");
// #endif
// 	// only parse token if there is no foreground running job
// 	for(int i = 0; i < MAX_JOB; i++){
// 		if(jobs[i].status == 2){
// 			fg_job_running = 1;
// 			break;
// 		}
// 	}
// 	if(!fg_job_running){
// 		// clear cmdbuffer and input buffer
// 		for(int i = 0; i < MAX_LINE; i++){
// 			if(!cmdbuffer[i])
// 			cmdbuffer[i] = 0;
// 		}
// 		while(getchar() != '\n');

// 		printf("prompt> ");
// 		if(scanf("%[^\n]", cmdbuffer)){ // scan the whole line (cmd and its args), only
// 										// extract the tokens if more than 0 character is
// 										// scaned
// 			do{
// 				argv[i] = strtok( i ? NULL : cmdbuffer, " \t\n");
// 			} while(argv[i] && ++i < MAX_ARGC);
// 		}
// #if DEBUG_ENALBED
// 		printf("• ended waiting cmd, cmd buffer contains:%s\n", cmdbuffer);
// #endif
// 		// put a NULL ptr at the end of scanned cmd arguments to use execv and execvp (see 'man exec')'
// 		// argv[i] = NULL;
// 		// NO NEED TO APPEND NULL BECAUSE WE ALREADY CLEAR THE ARGV WITH NULL AFTER PARSECMD
// #if DEBUG_ENALBED
// 		printf("• returned argc [%u] in parseTokens\n", (unsigned)i);
// #endif
// 	}
// 	return i; // argc

// Input redirecon to “input.txt” (Read)
// mode_t mode = S_IRWXU | S_IRWXG | S_IRWXO;
// FILE *inFileID = open(“input.txt”, O_RDONLY, mode);
// dup2(inFileID, STDIN_FILENO);
// // Output redirecon to “out.txt” (Create or Write)
// FILE *outFileID = open (“out.txt”, O_CREAT|O_WRONLY|O_TRUNC, mode);
// dup2(outFileID, STDOUT_FILENO);
// // Output append to “out.txt” (Create or Append)
// FILE *outFileID = open(“out.txt”, O_CREATE|O_APPEND|O_TRUNC, mode);
// dup2(outFileID, STDOUT_FILENO);

// }

// int cmdType(int argc, char **argv){
// 	// -1. invalid cmd
// 	// 0. jobs builtin
// 	// 1. quit builtin
// 	// 2. cd builtin
// 	// 3. fg builtin
// 	// 4. bg builin
// 	// 5. kill builtin
// 	// 6. fg general cmd
// 	// 7. bg general cmd
// 	if(argv[0]){
// 		if(!strcmp(*argv, "jobs")){ // builtin commands
// 			if(argc == 1) return 0;
// 		}
// 		else if(!strcmp(*argv, "quit")){
// 			if(argc == 1) return 1;
// 		}
// 		else if(!strcmp(*argv, "cd")){
// 			// check if path is valid
// 			if(argc == 2 && fopen(argv[1], "r")) return 2;
// 		}
// 		else if((!strcmp(*argv, "fg") || !strcmp(*argv, "bg") || !strcmp(*argv, "kill")) && argc == 2){
// 			// check if job_id or pid is valid
// 			int x_id;
// 			if(*argv[1] == '%'){
// 				x_id = atoi(argv[1] + 1);
// 			}
// 			else{
// 				x_id = atoi(argv[1]);
// 			}
// 			// proceed if atoi sucess
// 			if(x_id){
// 				for(int i = 0; i < MAX_JOB; i++){
// 					// jod id start with '%' and 1 <= # <= MAX_JOB, otherwise check for pid
// 					if(jobs[i].pid != -1 && ((x_id - 1 == i && argv[1][0] == '%') || x_id == jobs[i].pid)){
// 						jobs[i].action = 1;
// #if DEBUG_ENALBED
// 						printf("• job [%u] is marked for \"%s\"\n", jobs[i].pid, *argv);
// #endif
// 						if(!strcmp(*argv, "fg")) return 3;
// 						else if(!strcmp(*argv, "bg")) return 4;
// 						else return 5;
// 					}
// 				}
// 			}
// 			else{
// 				printf("Invalid job id or process id\n");
// 			}
// 		}
// #if DEBUG_ENALBED
// 		else if((!strcmp(*argv, "print"))){
// 			for(int i = 0; i < MAX_JOB; i++){
// 				printf("• jid:%i, pid:%u, status:%i, action:%u, cmd:%s\n", i + 1, jobs[i].pid, jobs[i].status, jobs[i].action, jobs[i].cmd);
// 			}
// 		}
// #endif
// 		else{ // possibly general commands, if the command is invalid the processGeneralCmd
// 			  // will handle that
// 			// check if '&' is at the end of the command for a background job
// 			if(argv[argc-1][0] == '&') return 7;
// 			// otherwise foreground
// 			else return 6;
// 		}
// 	}
// 	return -1;
// }

// int processBuiltInCmd(int command_type, int argc, char **argv){
// 	// All built-in cmds run in the foreground.
// 	// 0. quit
// 	// 1. failed or success
// 	switch(command_type){
// 		case 0: { // job builtin
// 					for(int i = 0; i < MAX_JOB; i++){
// 						if(jobs[i].pid != -1){
// 							const char *status;
// 							switch(jobs[i].status){
// 								 // -1 ?
// 								 // 2 ? No need to because the job must be dealth with
// 								 // before the current process can run
// 								case 0: status = "Stopped"; break;
// 								case 1: status = "Running"; break; // background running
// 								default: break;
// 							}
// 							printf("[%u] (%u) %s %s\n", i + 1, jobs[i].pid, status, jobs[i].cmd);
// 						}
// 					}
// 				} break;
// 		case 1: { // quit builtin
// 					// reap child processes in jobs
// 					for(int i = 0; i < MAX_JOB; i++){
// 						// terminate, send SIGKILL, SIGINT or SIGSTP to child ?
// 						// don't wait and kill all the child processes
// 						if(jobs[i].pid != -1){
// 							kill(jobs[i].pid, SIGINT);
// 						}
// 					}
// 					return 0;
// 				} break;
// 		case 2: { // cd builtin
// 					// get the curent working directory, the last char is reserved for \n
// 					// change the $PWD to the expanded path specified 

// 					// char cwd[MAX_PATH];
// 					// if(getcwd(cwd, MAX_PATH - 1)){
// 					// 	printf("Can't get the current working directory");
// 					// }
// 					if(chdir(argv[1])){
// 						printf("Failed to change to the specified file or directory\n");
// 					}
// 					// a string contains the current working directory ?
// 				} break;
// 		case 3: // -- DROP DOWN -- fg builtin, bring a bg running or stopped job to fg
// 		case 4: // -- DROP DOWN -- bg builin, bring a stopped job to the bg
// 		case 5: {// kill builtin
// 					struct job *j = NULL;
// 					int child_status;
// 					for(int i = 0; i < MAX_JOB; i++){
// 						if(jobs[i].action){
// 							j = jobs + i;
// 							// no more action needed
// 							j->action = 0;
// 							break;
// 						}
// 					}
// 					// continue stopped process
// 					if(command_type == 3 || command_type == 4){
// 						if(j->status == 0){ // stopped job, otherwise running background job
// #if DEBUG_ENALBED
// 							printf("• SIGCONT sent to job [%u]\n", j->pid);
// #endif
// 							kill(j->pid, SIGCONT); // send a continue signal to the stopped job
// 						}
// 						if(command_type == 3){ // fg
// 							// reassign job variable
// 							j->status = 2;
// 							// wait to reap the job when finished
// 							reapJob(j, 1);
// 						}
// 						else { // bg
// 							// rassign job variable and don't wait for the job to finish. Let
// 							// the signal handler handles the SIGCHLD
// 							// reassign job variable
// 							j->status = 1;
// 						}
// 					}
// 					else { // kill
// 						// send SIGINT to the child process, the signal handler will
// 						// handle the reaping of the process by handling SIGCHLD
// #if DEBUG_ENALBED
// 						printf("• SIGINT sent to job [%u]\n", j->pid);
// #endif
// 						kill(j->pid, SIGINT);
// 						// reset the job info
// 						j->pid = -1;
// 						j->status = -1;
// 					}
// 				} break;
// 	}
// 	return 1;
// }

// void processGeneralCmd(unsigned is_background, int argc, char **argv, char cmdbuffer[MAX_LINE]){
// 	// Issued general cmds (must be within the current directory) can either be running in the background or the foreground
// 	// TODO: need to return ?
// 	// 0. failed
// 	// 1. sucess
// 	int jid = lowestAvailJID();
// 	if(jid == -1){
// 		printf("No Job ID left to be used (max %u job(s))\n", MAX_JOB);
// 		// return 0;
// 	}
// 	else{
// 		// assign job info immediately before the child send SIGCHLD. Prioritize trival
// 		// member variables first before assigning pid
// 		jobs[jid].status = is_background ? 1 : 2;
// 		strcpy(jobs[jid].cmd, cmdbuffer);
// 		jobs[jid].pid = fork();
// 		if(jobs[jid].pid == -1){
// 			perror("• fork() error");
// 			// return 0;
// 		}
// 		else if(!jobs[jid].pid){ // child process
// 			// Execute, argv[0] is the command (executable binary or script), argv is the
// 			// argument for $0, $1, $2,...

// 			// Both execv and execvp can works with relative/ absolute path, the only difference is when only the file name is specified, execv will ONLY search for the local directory while the execvp will ONLY search in the $PATH
// 			// If execv failed, try execute with $PATH via execvp
// #if DEBUG_ENALBED
// 			// sleep(2);
// #endif
// 			if(execv(argv[0], argv) == -1 && execvp(argv[0], argv) == -1){
// 				// printf("Execv() failed\n");
// 				printf("Invalid command\n");
// 				// perror("• exec error");
// 				// exit immediately or the child process will continue where the parent left off
// 				exit(EXIT_FAILURE);
// 			}
// 		}
// 		else{ // current process
// 			// background child job forked, do not wait, only handle the SIGCHLD
// 			// signal to reap it
// 			if(!is_background){ // foreground child job forked
// 				reapJob(jobs + jid, 1);
// 			}
// 		}
// 	}
// 	// return 1;
// }

// int parseCmd(int argc, char **argv, char cmdbuffer[MAX_LINE]){
// 	// 0. quit
// 	// 1. failed or success (outputs are done in dedicated functions above)
// 	// check if there are command to be parsed
// 	int command_type = cmdType(argc, argv);
// 	switch(command_type){
// 		case -1: break; // invalid cmd
// 		case 0: // -- DROP DOWN -- jobs builtin
// 		case 1: // -- DROP DOWN -- quit builtin
// 		case 2: // -- DROP DOWN -- cd builtin
// 		case 3: // -- DROP DOWN -- fg builtin
// 		case 4: // -- DROP DOWN -- bg builin
// 		case 5: { // kill builtin
// 					// either return 1 or 0 (quit)
// 					return processBuiltInCmd(command_type, argc, argv);
// 				} break;
// 		case 6: // -- DROP DOWN -- fg general cmd
// 		case 7: { // bg general cmd
// 					processGeneralCmd(command_type - 6, argc, argv, cmdbuffer);
// 				} break;
// 	}
// 	return 1;
// }

// int main(){
// 	// TODO: redirection output > >>, input <
// 	// 1 input redirecon and at most one output redirecon (output redirect or output append)

// 	// define signal handler, is redefined in the signalHandler to avoid begin overwrited
// 	// with the default behavior
// 	signal(SIGCHLD, signalHandler);
// 	signal(SIGINT, signalHandler);
// 	signal(SIGTSTP, signalHandler);

// 	// MAX_ARGC arguments, the final one is reserve for a null pointer
// 	char *argv[MAX_ARGC + 1] = { [0 ... MAX_ARGC] = NULL };
// 	// 255 char max, the last is for \0
// 	char cmdbuffer[MAX_LINE] = { [0 ... MAX_LINE - 1] = 0 };
// 	int quit = 0;
// 	do{
// 			int argc = parseTokens(argv, cmdbuffer);
// 			// if cmdbuffer is not empty
// 			if(*cmdbuffer){
// 				if(!parseCmd(argc, argv, cmdbuffer)){
// 					quit = 1;
// 				}
// 				// if previous cmd is parsed
// 				// if(*argv){
// 					// clear input buffer to print the next prompt
// 					while(getchar() != '\n');
// 				// }
// #if DEBUG_ENALBED
// 				printf("• input buffer cleared\n");
// #endif
// 				// clear argv for the next cmd
// 				for(int i = 0; i < argc; i++){
// 					argv[i] = NULL;
// 				}
// 			}
// 	} while(!quit);
// 	// if(argc){ // only proceed if a command is given
// 	// 	switch(parseCmd(argc, argv, cmdbuffer)){
// 	// 		case -2: printf("All Job IDs are in used.\n"); break; // no job id avaialble
// 	// 		case -1: printf("Command not found.\n"); break; // invalid command
// 	// 		case 0: quit = 1; break;  // quit, TODO: REAP ALL CHILD PROCESSES
// 	// 		case 1: break;  // cmd parsed and processed sucessfully
// 	// 	}
// 	// }
// 	return 0;
// }

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
