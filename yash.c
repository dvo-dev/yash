#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Command.h"
#include "Job.h"
#include "ParseTools.h"

#define MAX_JOBS 20
#define DEBUG_MODE 1

typedef struct YashJobs {
  Job susTasks[MAX_JOBS];
  uint32_t susCtr;

  Job allTasks[MAX_JOBS];
  uint32_t taskCtr;

  Job fgTask;

  uint32_t jobCnt;
} YashJobs;

/** Globals :( */
pid_t yashPid;
pid_t chldPid;
YashJobs yashJobs = {
  {}, 0, {}, 0,
  { 0, NONE }, 0
};

/*-------------------- Global modifier functions --------------------*/
Job* pushTask(void) {
  return yashJobs.taskCtr >= MAX_JOBS ? NULL : &(yashJobs.allTasks[yashJobs.taskCtr++]);
}

Job* popTask(void) {
  return yashJobs.taskCtr <= 0 ? NULL : &(yashJobs.allTasks[--yashJobs.taskCtr]);
}

Job* pushSusTask(void) {
  return yashJobs.susCtr >= MAX_JOBS ? NULL : &(yashJobs.susTasks[yashJobs.susCtr++]);
}

Job* popSusTask(void) {
  return yashJobs.susCtr <= 0 ? NULL : &(yashJobs.susTasks[--yashJobs.susCtr]);
}

void setFgTask(pid_t pid, char* process) {
  yashJobs.fgTask = Job_new(pid, FG, process);
}

void setJobDone(pid_t pid) {
  for (int i=0; i<MAX_JOBS; ++i) {
    if (pid == yashJobs.allTasks[i].pid) {
      yashJobs.allTasks[i].state = DONE;
    }
  }
}

/*-------------------- Signal Handlers -------------------*/
void sigintHandler(int sigNum) {
  if (FG == yashJobs.fgTask.state) {
    kill(yashJobs.fgTask.pid, SIGINT);
    yashJobs.fgTask.state = NONE;
  }
#if DEBUG_MODE
  printf("Current task pid: %d\n", yashJobs.fgTask.pid);
#endif
}

void sigtstpHandler(int sigNum) {
  if (NONE == yashJobs.fgTask.state) {
    printf("No task to suspend\n");
    return;
  }
  Job_set(
    pushSusTask(),
    yashJobs.fgTask.pid,
    SUS,
    yashJobs.fgTask.process
  );
#if DEBUG_MODE
  printf("Sending STGTSTP to pid: %d\n", yashJobs.fgTask.pid);
#endif
  kill(yashJobs.fgTask.pid, SIGTSTP);
#if DEBUG_MODE
  printf(
    "[%d]+\tStopped\t\t\t%s\n",
    yashJobs.jobCnt, yashJobs.fgTask.process
  );
#endif
  Job_destroy(&yashJobs.fgTask);
}

void sigchldHandler(int sigNum) {
#if DEBUG_MODE
  printf("SIGCHLD signal %d received for pid: %d\n", sigNum, chldPid);
  printf("%d\n", getpid());
#endif
}

/*-------------------- Keyword command handlers -------------------*/
void sendToFg() {
  Job* susTask = popSusTask();
  if (!susTask) {
    printf("No suspended tasks\n");
    return;
  }
  Job_set(&yashJobs.fgTask, susTask->pid, FG, susTask->process);
  Job_destroy(susTask);
#if DEBUG_MODE
  printf("Sending SIGCONT to %s, pid: %d\n", yashJobs.fgTask.process, yashJobs.fgTask.pid);
#endif
  kill(yashJobs.fgTask.pid, SIGCONT);
  wait((int*) NULL);
  Job_destroy(&yashJobs.fgTask);
}

void sendToBg() {
  Job* susTask = popSusTask();
  if (!susTask) {
    printf("No suspended tasks\n");
    return;
  }
  Job* bgTask = pushTask();
  Job_set(bgTask, susTask->pid, BG, susTask->process);
  Job_reset(susTask);
  kill(bgTask->pid, SIGCONT);
  return;
}

void listJobs() {

}

/*-------------------- Shell operations --------------------*/
void Yash_redirect(Command* cmd) {
  /* Redirect stdin */
  if (cmd->fdTable.stdIn) {
    int ifd = open(
      cmd->fdTable.stdIn,
      O_RDONLY,
      S_IRGRP| S_IROTH | S_IRUSR | S_IWUSR
    );
    dup2(ifd, STDIN_FILENO);
    close(ifd);
  }

  /* Redirect stdout */
  if (cmd->fdTable.stdOut) {
    int ofd = open(
      cmd->fdTable.stdOut,
      O_CREAT | O_WRONLY,
      S_IRGRP | S_IROTH | S_IRUSR | S_IWUSR
    );
    dup2(ofd, STDOUT_FILENO);
    close(ofd);
  }

  /* Redirect stderr */
  if (cmd->fdTable.stdErr) {
    int efd = open(
      cmd->fdTable.stdErr,
      O_CREAT | O_WRONLY,
      S_IRGRP | S_IROTH | S_IRUSR | S_IWUSR
    );
    dup2(efd, STDERR_FILENO);
    close(efd);
  }

  return;
}

void Yash_executeCommand(Command* cmd) {
  pid_t pid = fork();
#if DEBUG_MODE
  printf("executeCommand for pid: %d\n", pid);
#endif

  /* Child executes the program */
  if (0 == pid) {
    Yash_redirect(cmd);
    execvp(cmd->program, Command_getArgs(cmd));
  /* Error */
  } else if (-1 == pid) {
    perror("fork error\n");
  /* Blocking until child finishes */
  } else {
    yashJobs.jobCnt+=1;
    if (!cmd->isBgTask) {
      Job_set(&yashJobs.fgTask, pid, FG, cmd->argStr);
#if DEBUG_MODE
      printf("Program: %s, pid: %d\n", yashJobs.fgTask.process, yashJobs.fgTask.pid);
#endif
      chldPid = pid;
      waitpid(pid, NULL, WUNTRACED | WCONTINUED);
      Job_reset(&yashJobs.fgTask);
    } else {
#if DEBUG_MODE
      printf("BG Program: %s, pid: %d\n", cmd->argStr, pid);
#endif
      Job_set(pushTask(), pid, BG, cmd->argStr);
      waitpid(pid, NULL, WNOHANG);
    }
  }
  
}

int Yash_forkPipes(Command* cmd) {
  int pipeFd[2];
  pipe(pipeFd);

  /* First child */
  pid_t pid0 = fork();
  chldPid = pid0;
  if (0 == pid0) {
    setpgid(0,0);
    close(pipeFd[0]);
    dup2(pipeFd[1], STDOUT_FILENO);
    Yash_redirect(cmd->pipe[0]);
    execvp(cmd->pipe[0]->program, Command_getArgs(cmd->pipe[0]));
  } else if (pid0 < 0) {
    perror("fork error\n");
  /* Parent process assigns stuff */
  } else {
    yashJobs.jobCnt+=1;
    if (!cmd->isBgTask) {
      Job_set(&yashJobs.fgTask, pid0, FG, cmd->argStr);
    } else {
      Job_set(pushTask(), pid0, BG, cmd->argStr);
    }
  }
 
  /* Second child */
  pid_t pid1 = fork();
  if (0 == pid1) {
    setpgid(0, pid0);
    close(pipeFd[1]);
    dup2(pipeFd[0], STDIN_FILENO);
    Yash_redirect(cmd->pipe[1]);
    execvp(cmd->pipe[1]->program, Command_getArgs(cmd->pipe[1]));
  } else if (pid1 < 0) {
    perror("fork error\n");
  }

  /* Cleanup pipes */
  close(pipeFd[0]);
  close(pipeFd[1]);
  if (!cmd->isBgTask) {
    waitpid(pid0, NULL, WUNTRACED|WCONTINUED);
    waitpid(pid1, NULL, WUNTRACED|WCONTINUED);
    Job_reset(&yashJobs.fgTask);
  } else {
    waitpid(pid0, NULL, WNOHANG);
    waitpid(pid1, NULL, WNOHANG);
  }
  return 0;
}

int main(int argc, char* argv[]) {
  /* Assign signal handlers */
  signal(SIGINT, sigintHandler);
  signal(SIGTSTP, sigtstpHandler);
  signal(SIGCHLD, sigchldHandler);

  yashPid = getpid();
  printf("yash pid: %d\n", yashPid);

  Job_reset(&yashJobs.fgTask);
  char* inString;
  while (inString = readline("# ")) {
    Vector* commands = Parse_commands(inString);

    while (Vector_size(commands) > 0) {
      Command* cmd = Vector_pop(commands);

      if (PIPE == cmd->type) {
        Yash_forkPipes(cmd);
      } else if (BG_CMD == cmd->type) {
        sendToBg();
      } else if (FG_CMD == cmd->type) {
        sendToFg();
      } else if (JOBS_CMD == cmd->type) {
        listJobs();
      } else {
        Yash_executeCommand(cmd);
      }
      Command_destroy(cmd);
    }

    Vector_destroy(commands);
  }
}
