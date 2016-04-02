// Assume no input line will be longer than 1024 bytes
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdbool.h>
#include <dirent.h>
#include <pwd.h>
#include <fcntl.h>
#include <termios.h>

#ifndef MAX_INPUT
#define MAX_INPUT 1024
#endif
#define HISTFILE home/.320sh_history
#define HISTORY_COUNT 50
#define NO_ID 0


typedef struct process
{
  int stop; 			//true is process has been suspended
  int complete;        		//true if process is completed
  int stat;			//status value
  struct process *next_process; //next process in pipe
  struct process *prev_process; //previous process in pipe
  char **allcommands;   	//to call execvp
  char * commands;
  pid_t pid;  			//process ID of the process
  
} process;

typedef struct job
{
  int jobnum;			//job number
  struct job *prev_job;         //previous job on list
  struct job *next_job;	 	//next job on list	
  pid_t pgid;			//group ID of process
  process *first_process;       //list of processes with same pgid
  int foreback;			//foreground = 1, background = 2
  struct termios tmodes;         //saved terminal 
  //int stderr, stdin, stdout;    //io channel
  int status;                   //status of job (0: done, 1: stopped, 					//2: terminated, 3: running)
} job;

extern job *job_list;


char* getInput();
char** getCommands(char* input);
int cd(char* path);
void getPwd(void);
int echo(char* string);
int set(char* envariable, char* resultvariable);
int ls(char** command);
int man(char** command);
int check_built_in(char ** allcommand, int endstatus, char* input);
int execcommand(char** command);
void addHistory();
int execProg(char** input);
int history(char *history[], int currentEntry);
int clear_history(char *history[]);
int file_histfile_exists();
bool parsefordirection(char** allcommands);
char** getcommandsbefore(char** allcommands, int currentpos);
char** getnextcommand (char** allcommands, int currentpos);
void outputdirection(char* filename, int handle);
bool inputdirection(char** command, char* filename);
void pipingprocess (char*** command, int size);



/*------------------job control functions---------------------*/


/*find process*/
process *pfind (pid_t pid);

/*check if all processes of a job is complete*/
int jcomplete (job *jobpt);

/*find job from process group id, returns pointer to found job*/
job *jfind (pid_t pgrpid);


/*initialize shell and put it in foreground, has control of terminal*/
void initialize_320sh ();

/*check if all processes of a job are complete or stopped*/
int jstop (job *jobpt);


 

/*create job information for new jobs*/
void start_job (job *jobpt, int foreback, char** allcommands);//fore: 0, back: 1

/*put job in foreground*/
void job_fore (job *jobpt, int cont);

/*put job in background*/
void job_back (job *jobpt, int cont);

void job_wait(job* jobptr);

/*lists all active jobs with information*/
void jobs (void);

void setInput();

void killcmd(char** command);

void checkForeBack(char** command);

void fg(char** command);

void bg(char** command);

//void init_signals(void);
void INThandler(int sig);

int update_status (pid_t pid, int status);

void setSignal(int onoff); //1: ignore, 0: reset default

