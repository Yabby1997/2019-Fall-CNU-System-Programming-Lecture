/* 
 * tsh - A tiny shell program with job control
 * shell-lab SYS00 201602022 SEUNGHUN YANG
 *
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */
/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */


/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
struct job_t {              /* The job struct */
	pid_t pid;              /* job PID */
	int jid;                /* job ID [1, 2, ...] */
	int state;              /* UNDEF, BG, FG, or ST */
	char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

extern char **environ;      /* defined in libc */
char prompt[] = "eslab_tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void waitfg(pid_t pid, int output_fd);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);


/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
	char c;
	char cmdline[MAXLINE];
	int emit_prompt = 1; /* emit prompt (default) */

	/* Redirect stderr to stdout (so that driver will get all output
	 * on the pipe connected to stdout) */
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "hvp")) != EOF) {
		switch (c) {
			case 'h':             /* print help message */
				usage();
				break;
			case 'v':             /* emit additional diagnostic info */
				verbose = 1;
				break;
			case 'p':             /* don't print a prompt */
				emit_prompt = 0;  /* handy for automatic testing */
				break;
			default:
				usage();
		}
	}

	/* Install the signal handlers */

	/* These are the ones you will need to implement */
	Signal(SIGINT,  sigint_handler);   /* ctrl-c */
	Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
	Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
	Signal(SIGTTIN, SIG_IGN);
	Signal(SIGTTOU, SIG_IGN);

	/* This one provides a clean way to kill the shell */
	Signal(SIGQUIT, sigquit_handler); 

	/* Initialize the job list */
	initjobs(jobs);

	/* Execute the shell's read/eval loop */
	while (1) {

		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		if (feof(stdin)) { /* End of file (ctrl-d) */
			fflush(stdout);
			fflush(stderr);
			exit(0);
		}

		/* Evaluate the command line */
		eval(cmdline);
		fflush(stdout);
		fflush(stdout);
	} 

	exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void eval(char *cmdline) 
{
	char *argv[MAXARGS];
	pid_t pid;
	int bg;
	sigset_t mask;

	bg = parseline(cmdline, argv);								//parseline함수의 반환값이 bg. background이면 true								
	sigemptyset(&mask);											//signal집합을 만들어 SIGCHILD, SIGINT, SIGTSTP를 넣는다. 
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGINT); 
	sigaddset(&mask, SIGTSTP);

	sigprocmask(SIG_BLOCK, &mask, NULL);						//add 전 제거되는것을 방지하기위해 block 

	if(!builtin_cmd(argv)){										//argv가built in이라면 실행후 종료, 아니v면
		if((pid = fork()) == 0){								//자식프로세스에 
			sigprocmask(SIG_UNBLOCK, &mask, NULL);				//언블락 
			if(execve(argv[0], argv, environ) < 0){				//해당 프로그램 실행새로운 자식  없으면 종료
				printf("%s : Command not found\n\n", argv);
				exit(0);
			}
		}

		addjob(jobs, pid, (bg == 1 ? BG : FG), cmdline);		//background면 background로 foreground면 foreground로
		sigprocmask(SIG_UNBLOCK, &mask, NULL);					//언블락

		if(!bg){												//background인경우와 그렇지 않은 경우에 맞게 처리
			while(1){											//pid가 FG의 pid랑 같지 않으면 문제가 있는것, break. 같다면 정상이므로 기다린다.
				if(pid != fgpid(jobs))							//fgpid는jobs에서status가 FG인 process의 pid를 반환 
					break;
				else
					sleep(1);									//아니라면 종료
			}
		}
		else{													//그렇지 않은 경우 백그라운드로 돌아가고있다는 메시지 출력
			printf("(%d) (%d) %s", pid2jid(pid), pid, cmdline);
		}
	}
	return;
}

int builtin_cmd(char **argv)
{
	char *cmd = argv[0];

	if(!strcmp(cmd, "quit")){
		exit(0);
	}

	else if(!strcmp(cmd, "jobs")){
		listjobs(jobs, 1);
		return 1;
	}

	return 0;
}

void waitfg(pid_t pid, int output_fd)
{
	return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	int child_status = 0;
	pid_t pid;

	while((pid = waitpid(-1, &child_status, WUNTRACED|WNOHANG)) > 0){		//종료/정지시까지 기다린다.
		if(WIFSIGNALED(child_status)){			//자식이 종료된 사유가 SIGINT라면 
			printf("Job  [%d]  (%d)  terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(child_status));	//jid, pid, 종료하도록 한 signal
			deletejob(jobs, pid);				//메시지를 출력하고 jobs에서 제거해준다.
		}
		else if(WIFSTOPPED(child_status)){
			getjobpid(jobs, pid)->state = ST;	//자식이 종료된 사유가 SIGTSTP라면 해당 pid의 상태를 stop으로 바꾼다. 제거된건 아니므로 제거는 x
			printf("Job  [%d]  (%d)  stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(child_status));		//jid, pid, 정지하도록 한 signal
		}
		else if(WIFEXITED(child_status)){		//정상종료된거라면 별다른 출력없이 종료시킨다. 
			deletejob(jobs, pid);
		}
		else{
			unix_error("waitpid error\n");
		}
	}	
	return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 					//sigint발생시 자동으로 호출되는 함수 
{
	pid_t pid = fgpid(jobs);					//foreground job 의 pid
	kill(pid, SIGINT);							//해당 pid로 SIGINT 시그널을 보낸다. 
	return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
	pid_t pid = fgpid(jobs);
	kill(pid, SIGTSTP);							//마찬가지로 해당 pid로 SIGTSTP시그널을 보낸다. 
	return;
}

/*********************
 * End signal handlers
 *********************/


/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{

	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array;          /* ptr that traverses command line */
	char *delim;                /* points to first space delimiter */
	int argc;                   /* number of args */
	int bg;                     /* background job? */

	strcpy(buf, cmdline);
	buf[strlen(buf)-1] = ' '; /* replace trailing '\n' with space */
	while(*buf && (*buf == ' '))
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	} 

	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0)
		argv[--argc] = NULL;

	return bg;

}

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {			//struct job이 인자로 전달됨
	int i;	

	for (i = 0; i < MAXJOBS; i++)			//최대 job 개수만큼 반복적으로 clearjob을 수행해줌. 
		clearjob(&jobs[i]);
}

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {			//struct job이 인자로 전달됨
	job->pid = 0;							//전달된 job의 pid와 jid를 0으로, 상태를 UNDEF로 초기화해줌 
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}


/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 				//struct job이 인자로 전달됨 
{
	int i, max=0;

	for (i = 0; i < MAXJOBS; i++)			//최대 job 개수만큼 반복적으로 jid를 모두 비교하며 최대 jid를 찾아 반환
		if (jobs[i].jid > max)
			max = jobs[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{											//struct job, pid, state, cmdline이 인자로 전달됨 
	int i;

	if (pid < 1)							//pid가 1보다 작은수라면 종료 
		return 0;

	for (i = 0; i < MAXJOBS; i++) {			//최대 job 개수만큼 반복하며 
		if (jobs[i].pid == 0) {		
			jobs[i].pid = pid;				//비어있는 인덱스의 job에 pid 설정, state, jid설정. nextjid 1 증가  
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)			//nextjid가 max를 넘어섯다면 nextjid는 1이된다. 
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);	//인자로입력한 cmdline도 복붙
			if(verbose){						//verbose가 true이면 
				printf("Added job. [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline); //메시지출력
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) //struct job, pid 인자로 
{
	int i;

	if (pid < 1)								//pid가 1미만이면 종료
		return 0;

	for (i = 0; i < MAXJOBS; i++) {				//입력된 pid와 같은 job을 찾아서
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);					//clearjob
			nextjid = maxjid(jobs)+1;			//nextjid
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
			return jobs[i].pid;
	return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
			return &jobs[i];
	return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return &jobs[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 							//pid를 인자로받아서 그에 해당하는 job의 jid를 반환 
{
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs, int output_fd) 					//job과 output_fd? 인자전달
{
	int i;
	char buf[MAXLINE];

	for (i = 0; i < MAXJOBS; i++) {									//다 돌리면서 jid pid 다 출력 
		memset(buf, '\0', MAXLINE);
		if (jobs[i].pid != 0) {
			sprintf(buf, "(%d) (%d) ", jobs[i].jid, jobs[i].pid);
			if(write(output_fd, buf, strlen(buf)) < 0) {
				fprintf(stderr, "Error writing to output file\n");
				exit(1);
			}
			memset(buf, '\0', MAXLINE);
			switch (jobs[i].state) {
				case BG:
					sprintf(buf, "Running    ");
					break;
				case FG:
					sprintf(buf, "Foreground ");
					break;
				case ST:
					sprintf(buf, "Stopped    ");
					break;
				default:
					sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",
							i, jobs[i].state);
			}
			if(write(output_fd, buf, strlen(buf)) < 0) {
				fprintf(stderr, "Error writing to output file\n");
				exit(1);
			}
			memset(buf, '\0', MAXLINE);
			sprintf(buf, "%s", jobs[i].cmdline);
			if(write(output_fd, buf, strlen(buf)) < 0) {
				fprintf(stderr, "Error writing to output file\n");
				exit(1);
			}
		}
	}
	if(output_fd != STDOUT_FILENO)
		close(output_fd);
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
	printf("Usage; shell [-hvp]\n");
	printf("   -h   print this message\n");
	printf("   -v   print additional diagnostic information \n");
	printf("   -p   do not emit a command prompt \n");
	exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
	struct sigaction action, old_action;

	action.sa_handler = handler;  
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}

