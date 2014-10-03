/***************************************************************************
 *  Title: Runtime environment
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  int job_no;
  char **argv;
  int argc;
  pid_t pid;
  struct bgjob_l* next;
} bgjobL;

/* the pids of the background processes */
// This holds a linked list of all the jobs
bgjobL *bgjobs = NULL;
// we might also need a a total of all background jobs

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);

static void PrintJob(bgjobL*, char*);

static void PrintJobsInReverse(bgjobL*);
/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

void RunCmdBg(commandT* cmd)
{
  // TODO
}

void RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf);
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

static void Exec(commandT* cmd, bool forceFork)
{
  pid_t child_pid = fork();
  int child_status;

  if(child_pid == 0) {
    // if you're in the child process
    execv(cmd->name, cmd->argv);

    printf("Exec failed\n");
    exit(0);
  } else {
    // you're in the parent
    if (cmd->bg)
    {
      // run in the background
      waitpid(child_pid, &child_status, WNOHANG);

      // add this job to the list
      bgjobL *new_job = malloc(sizeof(bgjobL));
      new_job->pid = child_pid;
      if (bgjobs == NULL) {
        new_job->job_no = 1;
      } else {
        new_job->job_no = bgjobs->job_no + 1;
      }
      new_job->argv = cmd->argv;
      new_job->argc = cmd->argc;
      new_job->next = bgjobs;
      bgjobs = new_job;

      // use sigprocmask here to block signals

    } else {
      waitpid(child_pid, &child_status, 0);
    }
  }
}

static bool IsBuiltIn(char* cmd)
{
  return (strcmp(cmd, "jobs") == 0);
}


static void RunBuiltInCmd(commandT* cmd)
{
  if (strncmp(cmd->argv[0], "jobs", 4) == 0) {
    PrintJobsInReverse(bgjobs);
  }
}

void PrintJobsInReverse(bgjobL *head)
{
  if (head == NULL)
    return;

  PrintJobsInReverse(head->next);

  int waitret, status;
  waitret = waitpid(head->pid, &status, WNOHANG| WUNTRACED);
  if (waitret == 0) {
    PrintJob(head, "Running");
  }
}

void DeleteJob(pid_t pid)
{
  bgjobL *curr = bgjobs;
  if (pid == bgjobs->pid) {
    bgjobs = curr->next;
    free(curr);
    return;
  } else {
  do {
    if (curr->next->pid == pid) {
      bgjobL *to_delete = curr->next;
      curr->next = to_delete->next;
      free(to_delete);
      }
    curr=curr->next;
    } while(curr && curr->next != NULL);
  }
}

void CheckJobs()
{
  bgjobL *curr = bgjobs;
  pid_t current_pid;

  int waitret, status;

  // iterate through all jobs
  while (curr != NULL) {
    current_pid = curr->pid;

    waitret = waitpid(current_pid, &status, WNOHANG);
    // check if the job is done
    if (waitret < 0) {
      PrintJob(curr, "Done");
      DeleteJob(current_pid);
    }
    curr=curr->next;
  }
}

void PrintJob(bgjobL *job, char* status)
{
  int k;
  printf("[%d]   %s                   %s", job->job_no, status, job->argv[0]); // how do we get the actual job name?
  for(k = 1; k < job->argc; k++) {
    printf(" %s", job->argv[k]);
    printf("\n");
  }
}


commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}
