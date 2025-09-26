// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10
#define MAX_HISTORY 10
#define MAX_CMD_LENGTH 100

// Command history
char history[MAX_HISTORY][MAX_CMD_LENGTH];
int history_count = 0;
int history_pos = 0;

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);
void panic(char*);
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));

// Simple strncmp implementation for xv6
int strncmp(const char *s1, const char *s2, int n)
{
  for(int i = 0; i < n; i++) {
    if(s1[i] != s2[i])
      return s1[i] - s2[i];
    if(s1[i] == 0)
      return 0;
  }
  return 0;
}

// Check if input is from terminal (not file)
int is_terminal_input(void)
{
  struct stat st;
  if(fstat(0, &st) < 0)
    return 0;
  return (st.type == T_DEVICE);
}

// Add command to history
void add_to_history(char *cmd)
{
  if(strlen(cmd) == 0) return;
  
  if(history_count > 0 && strcmp(history[history_count-1], cmd) == 0)
    return;
    
  if(history_count < MAX_HISTORY) {
    strcpy(history[history_count], cmd);
    history_count++;
  } else {
    for(int i = 0; i < MAX_HISTORY-1; i++) {
      strcpy(history[i], history[i+1]);
    }
    strcpy(history[MAX_HISTORY-1], cmd);
  }
  history_pos = history_count;
}

// Simple tab completion
char* complete_command(char *buf, int pos)
{
  char *commands[] = {"ls", "cat", "echo", "find", "grep", "sleep", 
                     "uptime", "cd", "mkdir", "rm", "sh", 0};
  
  for(int i = 0; commands[i]; i++) {
    if(strncmp(buf, commands[i], pos) == 0) {
      return commands[i];
    }
  }
  return 0;
}

// Enhanced getcmd with history, completion, and no prompt for files
int getcmd(char *buf, int nbuf)
{
  if(!is_terminal_input()) {
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if(buf[0] == 0)
      return -1;
    return 0;
  }
  
  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  
  int pos = 0;
  while(pos < nbuf - 1) {
    int cc = read(0, buf + pos, 1);
    if(cc < 1) break;
    
    if(buf[pos] == '\t') {
      char *completion = complete_command(buf, pos);
      if(completion) {
        int len = strlen(completion);
        if(len > pos) {
          strcpy(buf + pos, completion + pos);
          for(int i = pos; i < len; i++) {
            write(2, buf + i, 1);
          }
          pos = len;
        }
      }
    } else if(buf[pos] == '\n') {
      buf[pos] = 0;
      if(pos > 0) {
        add_to_history(buf);
      }
      return 0;
    } else if(buf[pos] == 0x7f || buf[pos] == '\b') {
      if(pos > 0) {
        pos--;
 //       write(2, "\b \b", 3);
      }
    } else if(buf[pos] == 0x1b) {
      char seq[2];
      if(read(0, seq, 2) == 2) {
        if(seq[0] == '[') {
          if(seq[1] == 'A') {
            if(history_pos > 0) {
              history_pos--;
              strcpy(buf, history[history_pos]);
              pos = strlen(buf);
           write(2, "\r$ ", 3);
              write(2, buf, pos);
            }
          } else if(seq[1] == 'B') {
            if(history_pos < history_count - 1) {
              history_pos++;
              strcpy(buf, history[history_pos]);
              pos = strlen(buf);
             write(2, "\r$ ", 3);
              write(2, buf, pos);
            } else if(history_pos == history_count - 1) {
              history_pos = history_count;
              buf[0] = 0;
              pos = 0;
              write(2, "\r$ ", 3);
            }
          }
        }
      }
    } else {
//      write(2, buf + pos, 1);
      pos++;
    }
  }
  
  if(pos == 0) return -1;
  buf[pos] = 0;
  return 0;
}

// Built-in history command
int history_command(void)
{
  for(int i = 0; i < history_count; i++) {
    printf("%d: %s\n", i+1, history[i]);
  }
  return 0;
}

// Enhanced runcmd with better background job handling
void runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
      
    if(strcmp(ecmd->argv[0], "history") == 0) {
      history_command();
      exit(0);
    }
    
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    int pid = fork1();
    if(pid == 0) {
      runcmd(bcmd->cmd);
    } else {
      printf("[%d] running in background\n", pid);
    }
    break;
  }
  exit(0);
}

int main(void)
{
  static char buf[100];
  int fd;

  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  while(getcmd(buf, sizeof(buf)) >= 0){
    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\n')
      continue;
      
    if(strcmp(cmd, "history") == 0) {
      history_command();
      continue;
    }
    
    if(cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' '){
      cmd[strlen(cmd)-1] = 0;
      if(chdir(cmd+3) < 0)
        fprintf(2, "cannot cd %s\n", cmd+3);
    } else {
      if(fork1() == 0)
        runcmd(parsecmd(cmd));
      wait(0);
    }
  }
  exit(0);
}

// === ORIGINAL FUNCTIONS (keep exactly as they were) ===

void panic(char *s)
{
 fprintf(2, "%s\n", s);
  exit(1);
}

int fork1(void)
{
  int pid;
  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

struct cmd* execcmd(void) {
  struct execcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd* redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd) {
  struct redircmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd* pipecmd(struct cmd *left, struct cmd *right) {
  struct pipecmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd* listcmd(struct cmd *left, struct cmd *right) {
  struct listcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd* backcmd(struct cmd *subcmd) {
  struct backcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int gettoken(char **ps, char *es, char **q, char **eq) {
  char *s;
  int ret;
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int peek(char **ps, char *es, char *toks) {
  char *s;
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd* parsecmd(char *s) {
  char *es;
  struct cmd *cmd;
  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
 //   fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd* parseline(char **ps, char *es) {
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd* parsepipe(char **ps, char *es) {
  struct cmd *cmd;
  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd* parseredirs(struct cmd *cmd, char **ps, char *es) {
  int tok;
  char *q, *eq;
  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd* parseblock(char **ps, char *es) {
  struct cmd *cmd;
  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd* parseexec(char **ps, char *es) {
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;
  if(peek(ps, es, "("))
    return parseblock(ps, es);
  ret = execcmd();
  cmd = (struct execcmd*)ret;
  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

struct cmd* nulterminate(struct cmd *cmd) {
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  if(cmd == 0)
    return 0;
  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;
  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;
  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;
  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;
  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
