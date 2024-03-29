#ifndef _COMMAND_H
#define _COMMAND_H

#include "Vector.h"

typedef struct FileDescriptorTable {
  char* stdIn;
  char* stdOut;
  char* stdErr;
} FileDescriptorTable;

typedef enum CommandType {
  UNDEFINED,
  EXECUTABLE,
  PIPE,
  BG_CMD,
  FG_CMD,
  JOBS_CMD
} TYPE;

typedef struct Command {
  char* program;
  Vector* arguments;
  char* argStr;
  int argLen;
  FileDescriptorTable fdTable;
  struct Command* pipe[2];
  TYPE type;
  int isBgTask;
} Command;

/*
 * Function:  createCommand
 * ------------------------
 * Initializes a Command struct
 * Allocates a pointer for a char string
 * Allocates a pointer for a StringVector
 * Initializes the argument length metadata to 0
 *
 * returns: An initalized Command struct
 */
Command* Command_new(void);

/*
 * Function:  destroyCommand
 * -------------------------
 * Deallocates all heap pointers used by Command:
 *    program (char*), arguments (StringVector*)
 * returns: none
 */
void Command_destroy(Command* cmd);
void Command_setProgram(Command* cmd, char* program);
void Command_pushArg(Command* cmd, char* arg);
void Command_print(Command cmd);
char** Command_getArgs(Command* cmd);
void Command_buildArgStr(Command* cmd, char* arg);

#endif /* COMMAND_H */
