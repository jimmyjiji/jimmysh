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
#include <sys/types.h>
#include "jimmysh.h"
#include <termios.h>
#include <stdarg.h>
#include <signal.h>


const char *histfilename;
char pwd[128];
//used for going back to previous path 
char* pathstack;

job *job_list = NULL;

//job *first_job = NULL;


//history buffer
int numQuotes = 0;
int current = 0; //current index of hist
int histPt = 0; //current index of hist pointed by up and down arrow
char *hist[HISTORY_COUNT];
bool debug;
pid_t currentProc;
pid_t csesh_pgid;
int bash_terminal;
int check_interactive;
struct termios cs320sh_tmodes;
int foreback = 0;
struct sigaction sigact;
int jobnum = 0;

void running(char* msg) {
  fprintf(stderr, "RUNNING: %s\n", msg);
}

void end(char* msg, int status) {
  fprintf(stderr, "ENDED %s(ret = %d)\n", msg, status);
}

int main (int argc, char ** argv, char **envp) {
  if (argv[1] != NULL && strcmp(argv[1], "-d") == 0) 
    debug = true;
  else
    debug = false;
     

  int finished = 0;
  char *prompt = "jimmysh> ";
  char *input;
  int io;
  int endstatus;
  getPwd();

  bash_terminal = STDIN_FILENO;
  check_interactive = isatty(bash_terminal);

  //initialize history
  int r = 0;
  for (r = 0; r < HISTORY_COUNT; r++)
    hist[r] = NULL;


  //malloc pathstack 
  pathstack = malloc(128);

  job_list = malloc(sizeof(job));

  if(file_histfile_exists()==0){
      write(1, "history file doesn't exist\n", strlen("history file doesn't exist\n"));
    }


  signal(SIGINT, INThandler);

  //just checking if shell is in foreground or background

  //just a check to see if the 320shell is in the foreground
  if(check_interactive == 1){
    if(tcgetpgrp(bash_terminal) == (csesh_pgid = getpgrp()))
      write(1, "320sh is in foreground\n", strlen("320sh is in foreground\n"));
    else
      write(1, "320sh is in background\n", strlen("320sh is in background\n"));


    while((csesh_pgid = getpgrp()) != tcgetpgrp(bash_terminal)){
      kill(- csesh_pgid, SIGTTIN);
    }

    // disable - ignore job control signals from terminal
    setSignal(1);
    job* ajob = malloc(sizeof(job));
    
    //create job and place shell into its own process group
    csesh_pgid = getpid();

    //set 320sh as first job in list
    ajob -> pgid = csesh_pgid;
    process *p = malloc(sizeof(process));
    p -> pid = csesh_pgid;
    p -> stop = 0;
    p -> complete = 0;
    p -> allcommands = NULL;
    p -> commands = "320sh";
    p -> next_process = NULL;
    job_list = ajob;
    ajob -> prev_job = NULL;
    ajob -> next_job = NULL;
    ajob -> first_process = p;
    ajob -> status = 3;
    ajob -> jobnum = jobnum;
    jobnum++;
    


    if (setpgid(csesh_pgid, csesh_pgid)<0){
      perror("failed to place shell in process group");
      exit(1);
    }
    //take control of the terminal
    tcgetattr(bash_terminal, &cs320sh_tmodes);
    tcsetpgrp(bash_terminal, csesh_pgid);
    ajob -> pgid = csesh_pgid;
  }

  while (!finished) {
    
    //inital prompt
    getcwd(pwd, 128);
    char initPrompt[128] = {"\e[1;34m["};
    strcat(initPrompt, pwd);
    strcat(initPrompt, "]");
    strcat(initPrompt, "\e[m");
    strcat(initPrompt, "\e[1;33m");
    strcat(initPrompt, prompt);
    strcat(initPrompt, "\e[m");
    io = write(1, initPrompt, strlen(initPrompt));
    
    
    if (!io) {
      finished = 1;
      break;
    }

    input = getInput();

    histPt = 0;
    //write(1, input, strlen(input));

    if(strncmp(input, "\0", 1)!=0){
    // add to history
      free(hist[current]);
      hist[current] = strdup(input);
      current = (current + 1) % HISTORY_COUNT;
    }

  
    char** allcommands = getCommands(input);

    bool directed = parsefordirection(allcommands);

  if (directed) 
    goto nextinput;

    int k = 0;
    int j = 0;
    while(allcommands[k] != NULL){
      j++;
      k++;
    }
    endstatus = check_built_in(allcommands, endstatus, input);
    
nextinput:
    // Execute the command, handling built-in commands separately 
    // Just echo the command line for now
    //write(1, cmd, strnlen(cmd, MAX_INPUT));
    
    free(allcommands);
    free(input);
    foreback = 0;
    //directed = false;
  }

return 0;
}

/*Gets the input line */

char* getInput() {
  int endposition = 0;
  char *buffer = malloc(sizeof(char) * MAX_INPUT);
  memset(buffer, 0,sizeof(char) * MAX_INPUT);
  char last_char = 1;
  char* cread = &last_char;
  int currentPos = 0;

  /*arrow key hex values
  1b 5b xx
  left - 44
  right -43
  up - 41
  down - 42
  */
  char* left = "\e[1D";
  char* right = "\e[1C";
  char* save = "\e[s";
  char* load = "\e[u";
  char* delete = "\e[K";
  int prevInput = 0;
  int towrite = 1;  


  while (1) {
    fflush(stdout);
    fflush(stdin);
    // Read a character
     if (read(0, cread, 1) == 0) {
      fprintf(stderr, "%s\n", "error reading");
      return NULL;
    }  
        
    if (*cread == '\n' && endposition < MAX_INPUT -1 && (numQuotes % 2 == false)) {
      write(1, "\n", 1);
      buffer[endposition] = '\0';
      return buffer;
    } else {

      
      if(*cread == 0x3){
       setSignal(0);
       write(1,"ctrl+c was pressed\n", strlen("ctrl+c was pressed\n"));

      } 
      else if(*cread == 0x1A){
        write(1,"ctrl+z was pressed\n", strlen("ctrl+z was pressed\n"));
      }


      //backspace
      else
       if(*cread == 0x7f){
        if (currentPos != 0 && endposition != 0) {
          char* deleteone = "\e[01D\e[K";
          if(currentPos<endposition){
            for(int i = currentPos-1; i < endposition-1; i++) {
              buffer[i] = buffer[i+1];
            }

            buffer[endposition-1] = '\0';
            
            
            write(1, save, strlen(save));

            char movecursor[] = "\e[";
            char num[2];
            sprintf(num, "%d", currentPos);
            strcat(movecursor, num);
            strcat(movecursor, "D");

            write(1, movecursor, strlen(movecursor));
            write(1, delete, strlen(delete));
            write(1, buffer, strlen(buffer));
            

            write(1, load, strlen(load));
            write(1, left, strlen(left));
            write(1, save, strlen(save));
            endposition--;
            currentPos--;
          } else if(currentPos == endposition){
            buffer[endposition] = '\0';
            currentPos--;
            endposition--;
            write(1, deleteone, strlen(deleteone));
          } 
        }
        towrite = 1;
        prevInput = 0;
      } 
      //add a space between % and x to treat % as seperate argument
      else if(buffer[currentPos-1] == '%' && (numQuotes % 2) == false ){
        buffer[currentPos] = ' ';
        buffer[currentPos+1] = *cread;
        currentPos+=2;
        endposition+=2;
        prevInput = 0;
        write(1, &last_char, 1);
        towrite = 0;
      }
      //add a space between x and & to treat & as seperate argument
      else if(*cread == '&' && (numQuotes % 2) == false && (buffer[currentPos-1] != ' ' || buffer[currentPos-1] != '\0')){
        buffer[currentPos] = ' ';
        buffer[currentPos+1] = *cread;
        currentPos+=2;
        endposition+=2;
        prevInput = 0;
        write(1, &last_char, 1);
        towrite = 0;
      }
      else if(*cread == 0x1b){
        towrite = 0;
        prevInput = 1;
      }
      else if((*cread == 0x5b)&&(prevInput == 1)){
        towrite = 0;
        prevInput = 1;
      } 
      else if(prevInput == 1){
        /* calls the history function here
         * first clears buffer
         * if *cread = 0x41 or 0x42 (up and down arrow)
         * calls history function (passing current buffer)
         * saves the returned string array as buffer
         * lastly returns the buffer to exit function
        */
       
         //uparrow
         //needs to clear buffer 
         int diff = 0;
         if(*cread == 0x41){
            if((current!=0)&&(histPt!=current)){

                if(buffer[0] == '\0'){

                   //print out the history entry 
                  histPt++;
                  diff = current - histPt;
                  write(1, hist[diff], strlen(hist[diff]));

                  //update the buffer with what is in the cmd line
                  buffer = strdup(hist[diff]);
                  currentPos = strlen(hist[diff]);
                  endposition = strlen(hist[diff]);

                } else{
                  write(1, save, strlen(save));

                  //first delete whatever is in the cmd line and move curson
                  char movecursor[] = "\e[";
                  char num[2];
                  sprintf(num, "%d", currentPos);
                  strcat(movecursor, num);
                  strcat(movecursor, "D");

                  write(1, movecursor, strlen(movecursor));
                  write(1, delete, strlen(delete));

                  //print out the history entry 
                  histPt++;
                  diff = current - histPt;
                  write(1, hist[diff], strlen(hist[diff]));

                  //update the buffer with what is in the cmd line
                  buffer = strdup(hist[diff]);
                  currentPos = strlen(hist[diff]);
                  endposition = strlen(hist[diff]);

                }                
            }
         }

         //also needs to clear buffer 
         //downarrow
         
         else if(*cread == 0x42){
            if(current!=0){
              if(histPt!=0){

                write(1, save, strlen(save));
                //first delete whatever is in the cmd line and move curson
                char movecursor[] = "\e[";
                char num[2];
                sprintf(num, "%d", currentPos);
                strcat(movecursor, num);
                strcat(movecursor, "D");

                write(1, movecursor, strlen(movecursor));
                write(1, delete, strlen(delete));

                //print out the history entry 
                histPt--;
                if(histPt!=0){
                  diff = current - histPt;
                  write(1, hist[diff], strlen(hist[diff]));

                  //update the buffer with what is in the cmd line
                  buffer = strdup(hist[diff]); 
                  currentPos = strlen(hist[diff]);
                  endposition = strlen(hist[diff]);
                }
                else{
                  buffer[0] = '\0';
                  diff = 0;
                  currentPos = 0;
                  endposition = 0;
                }
              }
            }
         }
        else if(*cread == 0x44){
          if (currentPos != 0) {
            currentPos--;
            write(1, left, strlen(left));
            write(1, save, strlen(save)); 
          }
        }
        else if(*cread == 0x43){
          if (endposition != currentPos) {
            currentPos++;
            write(1, right, strlen(right));
            write(1, save, strlen(save));
         }
        }
        towrite = 1;
        prevInput = 0;

      }else if((towrite == 1)&&(prevInput == 0)){
        if(currentPos == endposition){
          if (*cread != '\t') {
            if(*cread == '"')
              numQuotes++;
            buffer[endposition] = *cread;  
            endposition++;
            currentPos++;
            prevInput = 0;
            write(1, &last_char, 1);
          }
        } else if(currentPos<endposition){
          if(*cread == '"')
              numQuotes++;
          //inserting
          if (*cread != '\t') {
          endposition++;
          for(int k = endposition; k> currentPos; k--){
            buffer[k] = buffer[k-1];
          }
            buffer[currentPos] = *cread;
            currentPos++;
            prevInput = 0;
            

            write(1, delete, strlen(delete));
            write(1, buffer+currentPos-1, endposition-currentPos+1);
            write(1, load, strlen(load));
            write(1, right, strlen(right));
            write(1, save, strlen(save));
          }
        }
        towrite = 1;
        prevInput = 0;         
      }
    }
  }
}
  


/*Splits the input line by whitespace into tokens inputs  */
char** getCommands(char* input) {
  int endposition = 0;
  char** allCommands = malloc(MAX_INPUT * sizeof(char*));
  char *command = "";
  
  //split commands
  command = strtok(input, " \t\r\n\a");
  while (command != NULL) {
    allCommands[endposition] = command;
    endposition++;
    command = strtok(NULL, " \t\r\n\a");
  }

  //concat commands if a quote has been found
  bool foundquote = false;
  int counter = 0;
  for (int i = 0; i < endposition; i++) {
    char* checker = allCommands[i];
    //found a quotation 
    if (checker[0] == '\"' || checker[strlen(checker)-1] == '\"') {
      if (checker[0] == '\"' && checker[strlen(checker)-1] != '\"') {
         for(int k = 0; k < strlen(checker); k++) {
          checker[k] = checker[k+1];
        }
      }
      foundquote = true;
      //check if the quotation ends in the current command 
      if (checker[0] == '\"' && checker[strlen(checker)-1] == '\"') {
        //remove quotations 
        foundquote = false;
        for(int k = 0; k < strlen(checker); k++) {
          checker[k] = checker[k+1];
        }
        checker[strlen(checker)-1] = '\0';
      } else {
        //time to look for closing quations 
        //counts the number of commands going to concat
        counter = 0;
        //holds the original position of the opening quotation
        int holder = i+1;
        //looks for the next qutation 
        for (int k = i+1; k < endposition && foundquote; k++) {
          //this command has a quotation
          if (allCommands[k][strlen(allCommands[k])-1] == '\"') {
            foundquote = false;
            //make last char nullterminator 
            allCommands[k][strlen(allCommands[k])-1] = '\0';
            strcat(checker, " ");
            strcat(checker, allCommands[k]);

          } else if (allCommands[k][0] == '\"' && foundquote) {
            foundquote = false;
            char* morechecks = allCommands[k];
            for(int j = 0; j < strlen(morechecks); j++) {
                morechecks[j] = morechecks[j+1];
            }
            counter--;
          } else {
            //noquotation so just concat
            strcat(checker, " ");
            strcat(checker, allCommands[k]);            
          }
          counter++;
        }
         
          for(int i = holder; i < endposition-counter; i++) {
            allCommands[i] = allCommands[i+counter];
          }
          //change end position of commands
          endposition-=counter;
      }
    } 
  }
  //if a extra quote has been found, we go to the next line and ask user to input quote.
  if (foundquote) { 
     bool endsquote = false;
    if (allCommands[endposition-1][strlen(allCommands[endposition-1])-1] == '\"') {
      allCommands[endposition-1][strlen(allCommands[endposition-1])-1] = '\0';
      endsquote = true;
    }

    write(1, "> ", 2);
    char* moreinput = getInput();
    char* ptrtofree = moreinput;
    bool foundotherquote = false;
    
    if (endsquote) {
      char* ptrtoinput = moreinput;
      int counter = 0;
      while (*ptrtoinput != '\"') {
        ptrtoinput++;
        counter++;
      }
      *ptrtoinput = '\0';

      allCommands[endposition] = moreinput;
      ptrtoinput++;
      endposition++;
      char** morecommands = getCommands(ptrtoinput);
      char** startptrofcommands = morecommands;
      int commandlength = 0;
      while (*morecommands != 0) {
        commandlength++;
        morecommands++;
      }
      //append it to the end of this array
      for(int j = 0, i = endposition; j < commandlength; i++, j++) {
        allCommands[i] = startptrofcommands[j];
      }
      //update the position
      endposition+= commandlength;
      //free allocated commands
      free(startptrofcommands);
      free(moreinput);
    } else {
        while (!foundotherquote) {
          //if found the other quotation that ends the string 
          if (*moreinput == '\"' && *(moreinput+1) == '\0') {
            foundotherquote = true;
            break;
          } 
          //if found the other quotation but the string hasn't ended 
          else if (*moreinput == '\"') {
            moreinput++;
            //recursively call the method on itself 
            char** morecommands = getCommands(moreinput);
            char** startptrofcommands = morecommands;
            int commandlength = 0;
            while (*morecommands != 0) {
              commandlength++;
              morecommands++;
            }
            //append it to the end of this array
            for(int j = 0, i = endposition; j < commandlength; i++, j++) {
              allCommands[i] = startptrofcommands[j];
            }
            //update the position
            endposition+= commandlength;
            //free allocated commands
            free(startptrofcommands);
            break;
          }
          //if no quotations have been found , we go again to get more input 
          if (*moreinput == '\0') {
            write(1, "> ", 2);
            moreinput = getInput();
          }
          strncat(allCommands[endposition-1], moreinput, 1);
          moreinput++;   
        }
        //free allocated input 
        free(ptrtofree);
      }
  }
  allCommands[endposition] = NULL;
  return allCommands;
}



int cd (char* path) {
  //If no argument with cd, we cd to home 
  if (path == NULL) {
    strcpy(pathstack, pwd);
    char* home = getenv("HOME");
    chdir(home);
  } 
  //if .. is entered, we go back one directory 
  else if (strcmp(path, "..") == 0) {
    strcpy(pathstack, pwd);
    getPwd();
    char* path = pwd;
    char* ogpath = path;
    path += strlen(path)-1;
    while (*path != '/') {
      *path = '\0';
      path--;
    }
    chdir(ogpath);
  } 
  //if - is entered, we go back to previous directory on the path stack 
  else if (strcmp(path, "-") == 0) {
    chdir(pathstack);
    strcpy(pathstack, pwd);
  }
  //else we go to directory if it exists 
   else {
    strcpy(pathstack, pwd);
    if (chdir(path) != 0) {
      fprintf(stderr, "%s\n", "The directory does not exist");
      return -1;
    } 
  }
  return 1;
}

void getPwd() {
  getcwd(pwd, 128);
}

int echo(char* string ) {
  //if there is no string, prints new line 
  if (string == NULL)
    write(1, "\n", 2);
  else{
    //if $ is entered, we attempt to obtain an environment variable if exists 
    if (string[0] == '$') {
      string++;
      char* envariable = getenv(string);
      //print if environment variable exists 
      if (envariable != NULL)
        write(1, strcat(envariable, "\n"), strlen(envariable)+1);
    } else {
      //prints the string entered
      write(1, strcat(string, "\n"), strlen(string)+1);
    }
  } 

  return 1;
}

//set parameter envariable to resultvariable. Accessible via echo 
int set(char* envariable, char* resultvariable) {
  if (envariable == NULL || resultvariable == NULL){
    fprintf(stderr, "%s\n", "A variable is missing");
    return 0;
  }
  if (setenv(envariable, resultvariable, 1) != 0) {
    fprintf(stderr, "%s\n", "The environment variable was not set");
  } 
  return 1;

}

//executes program ls and its acoomplanying flags, not executed when flag invalid
int ls(char** command) {
  //int status;
  job* b = malloc(sizeof(job));
  start_job(b, foreback, command);
  return 1;
}

//executes program man and its function argument
int man(char** command){
  
  job* b = malloc(sizeof(job));
  start_job(b, foreback, command);

  return 1;
}

//executes all other programs
int execProg(char** command){
  job* b = malloc(sizeof(job));
  start_job(b, foreback, command);

  return 1;
}


//executes the command if user types ./ in child process 
int execcommand(char** command) {
  getPwd();
  int status;
  struct stat buf;
  char* run = command[0]+2;
  char* pathtorun = strcat(pwd, "/");
  strcat(pathtorun, run);

  if ((status = stat(pathtorun, &buf)) != 0){
    write(1, "This not a valid file to run", 28);
  }
  else {
     job* b = job_list;
     start_job(b, foreback, command);

  }
  return status;
}

int history(char *history[], int currentEntry){
  int k = currentEntry;
  int history_number = 1;

  do {
    if (history[k]) {
      char printline[MAX_INPUT];
      sprintf(printline, "%d", history_number);
      strcat(printline, ". ");
      strcat(printline, history[k]);
      write(1, printline, strlen(printline));
      write(1, "\n", 1);
      history_number++;
    }
   k = (k + 1) % HISTORY_COUNT;
  } while (k != currentEntry);
  return 1;
}

int clear_history(char *history[]){
  for(int j = 0; j < HISTORY_COUNT; j++){
    free(history[j]);
    history[j] = NULL;
    current = 0;
  }
  return 1;
}

void addHistory(){
  //get filelocation path
  struct passwd *pw = getpwuid(getuid());
  char *homedir = pw->pw_dir;
  char fileName[MAX_INPUT];
  sprintf(fileName, "%s/.320sh_history", homedir); 
  histfilename = fileName;


  FILE *fp = fopen(fileName, "wb");
  for(int k=0; k<current; k++){
    fwrite(hist[k], 1, strlen(hist[k]), fp);
    //write on next line
    fwrite("\n", sizeof(char), 1, fp);
  }

  if(fp!=NULL) 
    fclose(fp);
}
int file_histfile_exists(){
  //get filelocation path
  struct passwd *pw = getpwuid(getuid());
  char *homedir = pw->pw_dir;
  char fileName[MAX_INPUT];
  sprintf(fileName, "%s/.320sh_history", homedir); 
  histfilename = fileName;
  FILE *fp = fopen(fileName, "rb");
   //if file exists read
   if ( fp != NULL ) {
      char line [MAX_INPUT]; 
      while ( fgets ( line, sizeof line, fp ) != NULL ) {
         //load to history buffer and remove the last character '\n'
         char temp[strlen(line)];
         memcpy(temp, line, strlen(line)-1);
         hist[current] = strdup(temp);
         current++;
      }
      fclose ( fp );
      return(fp!=NULL);
   } else {
      fprintf(stderr, "couldn't open %s\n", fileName);
   }
   return 1;
}



bool parsefordirection(char** allcommands) {
  bool detecteddirection = false;
  int inputindexes[10] = {0};
  int outputindexes[10] = {0};
  int pipingindexes[10] = {0};
  int inputpointer = 0;
  int outputpointer = 0;
  int pipingpointer = 0;
  int outputhandle = 1;
  for (int i = 0; allcommands[i] != NULL; i++) {

      if (*allcommands[i] == '<'){
        detecteddirection = true;
        if(allcommands[i][1] == 0 && allcommands[i+1] != NULL) {
          inputindexes[inputpointer] = i;
          inputpointer++;
        } else {
          fprintf(stderr, "%s\n", "Invalid syntax");
          return detecteddirection;
        }
      }
      if (*allcommands[i] == '>' || allcommands[i][1] == '>' ){
        detecteddirection = true;
        if(allcommands[i][1] == 0 && *allcommands[i] == '>' && allcommands[i+1] != NULL) {
          outputindexes[outputpointer] = i;
          outputpointer++;
        } else if ((*allcommands[i]-'0') <= 2 && (*allcommands[i]-'0') >= 0 && allcommands[i][1] == '>' && allcommands[i+1] != NULL) {
          outputindexes[outputpointer] = i;
          outputhandle = *allcommands[i] - '0';
          outputpointer++;
        }
         else {
          fprintf(stderr, "%s\n", "Invalid syntax");
          return detecteddirection;
        }
      }

      if (*allcommands[i] == '|'){
        detecteddirection = true;
        if (allcommands[i][1] == 0 && allcommands[i+1] != NULL) {
          pipingindexes[pipingpointer] = i;
          pipingpointer++;
        }
       else {
          fprintf(stderr, "%s\n", "Invalid syntax");
          return detecteddirection;
       }
      }
  }

  inputpointer--;
  outputpointer--;

  if (detecteddirection && pipingindexes[0] == 0) {
    pid_t child_id = fork();
    char** firstcommand;
    int status;
      if (child_id == 0) {
        if (inputindexes[0] != 0) {
          int rightmostinput = inputindexes[inputpointer];
          int leftmostinput = inputindexes[0];
          char** commandsbefore_leftmost = getcommandsbefore(allcommands, leftmostinput);
          char* filename = allcommands[rightmostinput+1];
           if (!inputdirection(commandsbefore_leftmost, filename))  {
            free(commandsbefore_leftmost);
            exit(0);
          }
          free(commandsbefore_leftmost);
        }

        if (outputindexes[0] != 0) {
          int rightmostoutput = outputindexes[outputpointer];
          char* filename = allcommands[rightmostoutput+1];
          outputdirection(filename, outputhandle);
        }


        firstcommand = getnextcommand(allcommands, -1);
        execvp(firstcommand[0], firstcommand);
        free(firstcommand);
    } else {
      wait(&status);
    }
  } else if (detecteddirection && pipingindexes[0] != 0 && inputindexes[0] == 0 && outputindexes[0] == 0) {
    char** arrayofcommands[pipingpointer+1];
    for(int i = 0; i < pipingpointer+1; i++) {
       if (i == pipingpointer) 
        arrayofcommands[i] = getnextcommand(allcommands, pipingindexes[i-1]);
      else
        arrayofcommands[i] = getcommandsbefore(allcommands, pipingindexes[i]);
    }

    pipingprocess(arrayofcommands, pipingpointer);

    //free the allocated array 
    for (int i = 0; i < pipingpointer; i++) {
      free(arrayofcommands[i]);
    }
  } else if (detecteddirection && pipingindexes[0] != 0) {
    
  }
  
  return detecteddirection;
}

char** getcommandsbefore(char** allcommands, int currentpos) {
  char** commandsbeforehand = malloc(10*sizeof(char*));
        int commandsbeforehandcounter = 0;
        for (int j = 0, k = currentpos-1; k >= 0; j++, k--) {
          if (allcommands[k][0] == '>' || allcommands[k][0] == '<' || allcommands[k][0] == '|')
            break;
          commandsbeforehand[j] = allcommands[k];
          commandsbeforehandcounter++;
        }
        commandsbeforehandcounter--;
        int start = 0;
        int end = commandsbeforehandcounter;
        while (start < end) {
          char* temp = commandsbeforehand[start];   
          commandsbeforehand[start] = commandsbeforehand[end];
          commandsbeforehand[end] = temp;
          start++;
          end--;
        }
        commandsbeforehand[++commandsbeforehandcounter] = NULL;   
  return commandsbeforehand;
}


char** getnextcommand (char** allcommands, int currentpos) {
  char** firstcommand = malloc(10*sizeof(char*));
  int end = 0;
  for(int j = 0, i = currentpos+1; allcommands[i] != NULL; j++, i++) {
    if (*allcommands[i] == '<' || (*allcommands[i] == '>' || allcommands[i][1] == '>') || *allcommands[i] == '|')
      break;
    firstcommand[j] = allcommands[i];
    end++;
  }
  firstcommand[end] = NULL;
  return firstcommand;
}

void outputdirection(char* filename, int handle) {
  fflush(0);
    //clears the file content 
    unlink(filename);
    int descriptor;

    switch(handle) {
      case 0: descriptor = STDIN_FILENO; break;
      case 1: descriptor = STDOUT_FILENO; break;
      case 2: descriptor = STDERR_FILENO; break;
      default:
        write(1, "incorrect descriptor", 20);
        return;
    }
 
    int fd = open(filename, O_WRONLY| O_CREAT, 0666); // Open a file with fd no = 1
    dup2(fd, descriptor);
    close(fd);
    //runs the command into the file 
}

bool inputdirection(char** command, char* filename) {
  fflush(0);
  struct stat buf;
  int exists = stat(filename, &buf);
  if (exists == 0) {
      int fd = open(filename, O_RDONLY, 0);
      dup2(fd, STDIN_FILENO);
      close(fd);
      return true;
  } else {
    char* error = strcat(filename, ": No such file or directory\n");
    write(1, error, strlen(error));
    return false;
  }
}


void pipingprocess (char*** arrayofcommands, int size) {
  pid_t child_pid;
  int pipes[2];
  int inputfd;
  int status;
  fflush(0);

  for (int i = 0; i < size+1; i++) {
    pipe(pipes);  
    if ((child_pid = fork()) == 0) {
      //close the input side 
      close(pipes[0]);
      //set the input file descriptor to standard in
      dup2(inputfd, STDIN_FILENO); 

      // if there is a next command, we preset writing to standard out 
      if ((arrayofcommands[i+1]) != NULL)
        dup2(pipes[1], STDOUT_FILENO);

      execvp(arrayofcommands[i][0], arrayofcommands[i]);
    } else {
      //wait on dem children
      wait(&status);
      //close writing side because when it loops, you want to read in the next command
      close(pipes[1]);
      //set the next input file descriptor from the writing part of the pipe 
      inputfd = pipes[0]; 
    }
  }
}

int check_built_in(char** allcommands, int endstatus, char* input){
  int endstat = endstatus;
  checkForeBack(allcommands);
  if (allcommands[0] == NULL) {
     //nothing
    }
    else if (strcmp("exit", allcommands[0]) == 0) {
      free(input);  
      free(allcommands);
      free(pathstack);
      free(job_list);
      addHistory();
      exit(EXIT_SUCCESS);
    } 
    else if (allcommands[0][0] == '.' && allcommands[0][1] == '/') {
      if (debug)
        running("./");
      endstat = execcommand(allcommands);
      if (debug)
        end("./", endstat);
    }
    else if (strcmp("kill", allcommands[0]) == 0){
      killcmd(allcommands);
    }


    else if (strcmp("pwd", allcommands[0]) == 0) {
      if (debug)
        running("pwd");
      getPwd();
      write(1, strcat(pwd,"\n"), strlen(pwd)+1);
      if (debug)
        end("pwd", EXIT_SUCCESS);
    }
    else if (strcmp("help", allcommands[0]) == 0) {
      if (debug)
        running("help");
      char* msg1 = "DJ's Shell\n";
      char* msg2 = "Type program names and arguments, then hit enter\n";
      char* msg3 = "Built in functions: \n   cd\n   help\n   exit\n";
      char* msg4 = "Additional Shell Functions: \n   pwd\n   ls\n   history\n   clear-history\n";
      write(1, msg1, strlen(msg1));
      write(1, msg2, strlen(msg2));
      write(1, msg3, strlen(msg3));
      write(1, msg4, strlen(msg4));
     if (debug)
        end("help", 1);
    }

    else if (strcmp("cd", allcommands[0]) == 0) {
      if (debug)
        running("cd");
      endstat = cd(allcommands[1]);
      if (debug)
        end("cd", endstat);
    }

    else if (strcmp("echo", allcommands[0]) == 0){
      if (debug)
        running("echo");
      endstat = echo(allcommands[1]);
      if (debug)
        end("echo", endstat);
    }

    else if (strcmp("set", allcommands[0]) == 0){
      if (debug)
        running("set");
      endstat = set(allcommands[1], allcommands[2]);
      if (debug)
        end("set", endstat);
    }
    else if(strcmp("jobs", allcommands[0]) == 0){
      if (debug)
        running("jobs");
        jobs();
      if (debug)
        end("jobs", 1);
    }

    else if (strcmp("ls", allcommands[0]) == 0){
      if (debug)
        running("ls");
      endstat = ls(allcommands);
      if (debug)
        end("ls", endstat);
    }
    else if (strcmp("man", allcommands[0]) == 0){
      if (debug)
        running("man");
      endstat = man(allcommands);
      if (debug)
        end("man", endstat);
    } 
    //history functions
    else if(strcmp("history", allcommands[0]) == 0){
      if (debug)
        running("history");
      endstat = history(hist, current);
      if (debug)
        end("history", endstat);
    }
    else if(strcmp("clear-history", allcommands[0]) == 0){
      clear_history(hist);
    } else {
      if (debug)
        running("command");
      endstat = execProg(allcommands);
      if (debug)
        end("command", endstat);
    }
    return endstat;
}


void checkForeBack(char ** command){
  int k = 0;
  int a = 0;
  while(command[k] != NULL && foreback != 1){
    a++;
    if(strcmp(command[k], "&") == 0){
      foreback = 1;
      command[k] = NULL;
    }  
    k++;
  }

}

/*------------------------------Job control functions--------------------------------------*/

//loop through the job list for the matching job based on the pgid, otherwise return NULL
void sigint_handler(int sig)
{
  signal(SIGINT, SIG_DFL);
  kill(getpid(), SIGINT);

}

void jobs(){
  job *list = job_list;
  char *num; 
  char* jobstatus;
  //char** commands;
  //char* cmdline;
  int jnum = 0;

  while(list!=NULL){
    jnum++;
    char jobnumber[128];
    char pid[128];
    char *first = "[";
    num = strdup(first);
    sprintf(jobnumber, "%d", list-> jobnum);
    strcat(num, jobnumber);
    strcat(num, "]");

    strcat(num, "  ");
    strcat(num, "(");
    sprintf(pid, "%d", list-> first_process -> pid);
    strcat(num, pid);
    strcat(num, ")       ");


    if(list -> status == 1){
      jobstatus = strdup("Stopped");
    } else if(list -> status == 2){
      jobstatus = strdup("Terminated");
    } else if(list -> status == 3){
      jobstatus = strdup("Running");
    } else if(list -> status == 0){
      jobstatus = strdup("Done");
    } 

    strcat(num, "   ");
    strcat(num, jobstatus);
    strcat(num, "            ");
  
    strcat(num, list -> first_process -> commands);

    printf("number of jobs: %d\n", jnum);
    printf("Job ID: %d\n", list-> first_process -> pid);

    printf("%s\n", num);


    list = list -> next_job;
  }  
}

void fg(char** command){

}

void bg(char** command){

}

void killcmd(char** allcommands){
 job* j = job_list;
      int jobnumber = atoi(allcommands[1]);
      pid_t pgid = 0;
      pid_t pid = 0;
      if(strcmp("%", allcommands[1]) == 0){
        while(j != NULL){
          jobnumber = atoi(allcommands[2]);
          if(j -> jobnum == jobnumber)
          {
            pgid = j -> first_process -> pid;
            goto result;
          }
          j = j -> next_job;
        }
result: 
        if(pgid != 0){
          job* jo = jfind(pid);
          jo -> status = 2;
          kill(pgid, SIGTERM);
        }
        else
         write(1, "No job with that number found\n", 32);     
      } else{

        pid = atoi(allcommands[1]);

        process *p = pfind(pid);
        if(p != NULL){
            job* jj = jfind(pid);
            jj -> status = 2;        
            setSignal(0);
            kill(p->pid, SIGTERM);
          } else
         write(1, "No process with that PID found\n", 32);     
      } 
       
}

job* jfind(pid_t pgid){
  job* j = job_list;
  while(j != NULL){
    if(j -> pgid == pgid)
      return j;
    j = j -> next_job;
  }
  return NULL;
}

process* pfind(pid_t pid){
  pid_t p = pid;
  job* j = job_list;
  while(j != NULL){
    if(j -> first_process -> pid == p)
      return j -> first_process;
    j = j -> next_job;
  }
  return NULL;
}

int jcomplete (job *jobpt){
  process *pptr = jobpt -> first_process;
  while(pptr != NULL){
    if(pptr -> complete == 0) //0 is not complete, 1 is complete
      return 0;
    pptr = pptr -> next_process;
  }
  return 1;
}

int jstop(job *jobpt){
  process *pptr = jobpt -> first_process;
  while(pptr != NULL){
    if(pptr -> complete != 1 && pptr -> stop != 1) //0 is not complete, 1 is complete
      return 0;
    pptr = pptr -> next_process;
  }
  return 1;
}

void start_job(job *jobptr, int foreback, char**commands){
  process *pptr = malloc(sizeof(process));
  pid_t pid;
  
  //int status;
  job* ajob = jobptr;
  job* bjob = job_list;

  //loop through jobs so new job is at end of list
  while(bjob != NULL){
    if(bjob -> next_job == NULL){
      bjob -> next_job = ajob;
      break;
    }
    else
      bjob = bjob -> next_job;
  }
  ajob -> first_process = pptr;
  ajob -> next_job = NULL;
  ajob -> jobnum = jobnum;
  jobnum++;
  pptr -> next_process = NULL;
  pptr -> allcommands = commands;

  char *cmdline;
      if(pptr -> allcommands != NULL){
        int a = 0;
        cmdline = strdup(pptr -> allcommands[0]);
        a++;
        while(pptr -> allcommands[a] != NULL){
          strcat(cmdline, pptr -> allcommands[a]);
          strcat(cmdline, " ");
          a++;
        }
        pptr -> commands = cmdline;
      } else{
        pptr -> commands = "NULL";
      }


  if((pid = fork()) == 0){
    pid_t procid;

    if(check_interactive)
    {
      /*This is where we place the process into its appropriate group and give the group access to terminal
       depending on if its foreground or background*/
      procid = getpid();
      currentProc = procid;
      pptr -> pid = procid;
      pptr -> next_process = NULL;
      
      


      if(ajob-> pgid == NO_ID){
        ajob-> pgid = procid;
        setpgid(procid, ajob -> pgid);
      }



      //give the process group the terminal if its foreground
      if(foreback == 0)
        tcsetpgrp(bash_terminal, ajob-> pgid);
      else if(foreback == 1){
        tcsetpgrp(bash_terminal, csesh_pgid);
      }

      //reset signals
      setSignal(0);
      ajob -> status = 3; //running until otherwise
      execvp (pptr -> allcommands[0], pptr -> allcommands);
      if(foreback == 1){
        ajob -> status = 1; //running until otherwise
        setSignal(0);
        kill(procid, SIGSTOP);
      }

      ajob-> status = 2;
      perror("No such command");
      exit(1);
      //exit(1);
    }
  }
  else if(pid < 0){
    //fork failed
    perror ("fork");
    exit(1);
  }
   
  else{
    
    pptr->pid = pid;
    if(!check_interactive){
      if(ajob-> pgid > 0)
        ajob -> pgid = pid;
      setpgid(pid, ajob -> pgid);
    }

    if(!check_interactive)
      job_wait(ajob);
    else if (foreback == 0){
      ajob -> status = 0;
      job_fore(ajob, 0);
    } else if(foreback == 1){
      ajob -> status = 1;
      job_back(ajob, 0);
    }
   
  }
}


void setSignal(int onoff){
  if (onoff == 0){
    signal (SIGTTOU, SIG_DFL);
    signal (SIGINT, SIG_DFL);
    signal (SIGCHLD, SIG_DFL);
    signal (SIGTTIN, SIG_DFL);
    signal (SIGQUIT, SIG_DFL);
    signal (SIGTSTP, SIG_DFL);
  } else if(onoff == 1){
    signal (SIGCHLD, SIG_IGN);
    signal (SIGTTIN, SIG_IGN);
    signal (SIGTTOU, SIG_IGN);
    signal (SIGINT, SIG_IGN);
    signal (SIGQUIT, SIG_IGN);
    signal (SIGTSTP, SIG_IGN);
  }
}

/* Put job j in the foreground.  If cont is nonzero,
   restore the saved terminal modes and send the process group a
   SIGCONT signal to wake it up before we block.  */

void job_fore (job *j, int cont)
{
  //int status;
  /* Put the job into the foreground.  */
  tcsetpgrp (csesh_pgid, j->pgid);

  /* Send the job a continue signal, if necessary.  */
  if (cont)
    {
      tcsetattr (csesh_pgid, TCSADRAIN, &j->tmodes);
      if (kill (- j->pgid, SIGCONT) < 0)
        perror ("kill (SIGCONT)");
    }
  
  /* Wait for it to report.  */
  j -> status = 3; //running
  job_wait(j);
  /* Put the shell back in the foreground.  */
  tcsetpgrp (bash_terminal, csesh_pgid);

  /* Restore the shell’s terminal modes.  */
  tcgetattr (csesh_pgid, &j->tmodes);
  tcsetattr (csesh_pgid, TCSADRAIN, &cs320sh_tmodes);
}

/* Put a job in the background.  If the cont argument is true, send
   the process group a SIGCONT signal to wake it up.  */

void job_back (job *j, int cont)
{

  /* Put the shell back in the foreground.  */
  tcsetpgrp (bash_terminal, csesh_pgid);
  j -> status = 1;
  /* Send the job a continue signal, if necessary.  */
  if (cont)
    if (kill (-j->pgid, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
}

void  INThandler(int sig){
 signal(sig, SIG_DFL); /* reset signal */
 kill(currentProc, SIGINT);
   write(1, "CHILD: I have received a SIGINT\n", strlen("CHILD: I have received a SIGINT\n"));
  exit(1);

}

void job_wait(job *jobptr){
  int stat;
  pid_t processid;
  do
  {
    processid = waitpid (WAIT_ANY, &stat, WUNTRACED);
  }
  while (!jcomplete (jobptr) &&!update_status (processid, stat) && !jstop (jobptr));
}

int update_status (pid_t pid, int status)
{
  job *j = job_list;
  process *p = j -> first_process;
  if (pid > 0)
    {
      /* update process information  */
      while(j!=NULL){
        while(p!= NULL){
          if (p->pid == pid)
            {
              p->stat = status;
              if (WIFSTOPPED (status))
                p->stop = 1;
              else
                {
                  p->complete = 1;
                  if (WIFSIGNALED (status)){
                    int a = (int) pid;
                    int b = WTERMSIG (p->stat);
                    char pidstr[20];
                    char statstr[20];

                    sprintf(pidstr, "%d", a);
                    sprintf(statstr, "%d", b);
                    char* statment = malloc(sizeof(char));
                    strcat(statment, pidstr);
                    strcat(statment, ": ");
                    strcat(statment, "process terminated by ");
                    strcat(statment, statstr);
                    strcat(statment, "\n");

                    write(1, statment, strlen(statment));
                    free(statment);
                  }
                }
              return 0;
             }
             p = p -> next_process;
          }
          j = j -> next_job;
      }
      char pidstr[20];
      sprintf(pidstr, "%d", pid);
      char* statment = "There is no child process of ";
      strcat(statment, pidstr);
      strcat(statment, "\n");
      write(1, statment, strlen(statment));  
      return -1;
    }
  else if (pid == 0)
    return -1;
  else 
    return -1;
}