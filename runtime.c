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

typedef enum {RUNNING, STOPPED, DONE, TERMINATED} status_no;

typedef struct bgjob_l {
  int job_no;
  char **argv;
  char *cmdline;
  int argc;
  pid_t pid;
  status_no status;
  bool was_bg;
  struct bgjob_l* next;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

pid_t fg_job = 0;

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

static void PrintJob(bgjobL*);

static void PrintJobsInReverse(bgjobL*);

static void addjob(commandT*, pid_t, bool was_bg);

void StopFgProc();

void PrintArgs(bgjobL*);

void MarkAs(pid_t, int status);

int findLowestJobNo();

bgjobL* GetJobByPid(pid_t);

/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else {
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
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask,SIGCHLD);
  sigaddset(&mask,SIGTSTP);
  sigaddset(&mask,SIGINT);
  sigprocmask(SIG_BLOCK,&mask,NULL);

  pid_t child_pid = fork();

  if(child_pid == 0) {
    // if you're in the child process
    setpgid(0,0);
    sigprocmask(SIG_UNBLOCK,&mask,NULL);
    execv(cmd->name, cmd->argv);

    printf("Exec failed\n");
  } else {
    if (cmd->bg)
    {
      addjob(cmd, child_pid, 1);
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
    } else {
      addjob(cmd, child_pid, 0);
      fg_job = child_pid;
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
      while (waitpid(child_pid, NULL, WNOHANG|WUNTRACED) == 0) {

      }
      if (GetJobByPid(fg_job)->status != STOPPED)
        MarkAs(fg_job, DONE);
    }
  }
}

static void addjob(commandT* cmd, pid_t child_pid, bool was_bg)
{
    bgjobL *new_job = malloc(sizeof(bgjobL));
    new_job->pid = child_pid;
    new_job->job_no = findLowestJobNo();
    new_job->cmdline = cmd->cmdline;
    new_job->next = bgjobs;
    new_job->status = RUNNING;
    new_job->was_bg = was_bg;
    bgjobs = new_job;
}

int findLowestJobNo()
{
  bgjobL *curr = bgjobs;
  int num = 1;
  while (curr != NULL) {
    if (curr->status != DONE) {// && curr->status != TERMINATED) {
      if (curr->job_no >= num)
        num = curr->job_no + 1;
    }
    curr = curr->next;
  }
  return num;
}

void MarkAs(pid_t pid, int status)
{
  bgjobL *curr = bgjobs;

  while (curr != NULL) {
    if (curr->pid == pid)
      curr->status = status;
    curr = curr->next;
  }
}

bgjobL* GetJobByPid(pid_t pid)
{
  bgjobL *curr = bgjobs;
  while (curr != NULL) {
    if (curr->pid == pid)
      return curr;
    curr = curr->next;
  }
  return NULL;
}

static bool IsBuiltIn(char* cmd)
{
  return (strcmp(cmd, "jobs") == 0 ||
          strcmp(cmd, "cd") == 0 ||
          strcmp(cmd, "fg") == 0 ||
          strcmp(cmd, "bg") == 0);
}

static void RunBuiltInCmd(commandT* cmd)
{
  if (strncmp(cmd->argv[0], "jobs", 4) == 0) {
    PrintJobsInReverse(bgjobs);
  }
  else if(strncmp(cmd->argv[0], "cd", 2) == 0) {
    if (!chdir(cmd->argv[1]))
      printf("Error changing directory\n");
  } /*
  else if(strncmp(cmd->argv[0], "fg", 2) == 0) {
    bgjobL *curr = bgjobs;
    kill(curr->pid, SIGCONT);
    wait(&curr->pid);
  } else if(strncmp(cmd->argv[0], "bg", 2) == 0) {
    bgjobL *curr = bgjobs;
    printf("[%d]  ", curr->job_no);
    kill(curr->pid, SIGCONT);
  }
  */
}

void PrintJobsInReverse(bgjobL *job)
{
  if (job == NULL)
    return;

  PrintJobsInReverse(job->next);

  if (job->status == STOPPED) {
    PrintJob(job);
  } else if (job->status == RUNNING) {
    PrintJob(job);
  }
}

void freeAllJobs()
{
  bgjobL *curr = bgjobs;
  bgjobL *next;
  while (curr != NULL) {
    next = curr->next;
    free(curr);
    curr = next;
  }
}

void CheckJobs()
{
  bgjobL *curr = bgjobs;

  while (curr != NULL) {
    if(curr->was_bg &&
      (waitpid(curr->pid, NULL, WNOHANG|WUNTRACED) < 0) &&
      (curr->status != DONE))
      {
        curr->status = DONE;
        PrintJob(curr);
      }
    curr=curr->next;
  }
}

void PrintJob(bgjobL *job)
{
  char *status;
  if (job->status == RUNNING)
    status = "Running";
  else if (job->status == STOPPED)
    status = "Stopped";
  else
    status = "Done";

  printf("[%d]   %s                   %s", job->job_no, status, job->cmdline);
  if (job->was_bg && job->status == RUNNING)
    printf("&");
  printf("\n");
  fflush(stdout);
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

void StopFgProc() {
  if (fg_job) {
    if (kill(-fg_job, SIGTSTP) != 0)
      printf("Error in kill\n");
    else {
      printf("\n");
      fflush(stdout);
      MarkAs(fg_job, STOPPED);
      PrintJob(GetJobByPid(fg_job));
    }
  }
}

void TerminateFgProc() {
  if (fg_job) {
    if (kill(fg_job, SIGINT) != 0)
      printf("kill error\n");
    else
      MarkAs(fg_job, DONE);
  }
}

/*
void SigChldHandler() {
  int status, pid;
  while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0) {
    if (!WIFSTOPPED(status)) {
      MarkAs(pid, DONE);
    }
  }
}
*/
