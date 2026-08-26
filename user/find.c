#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* eargv[32];
int eargc = 0;
int do_exec = 0;

char*
fmtname(char* path)
{
  char* p;

  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  return p;
}

void
run(char* path)
{
  if (!do_exec) {
    printf("%s\n", path);
    return;
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "find: fork failed\n");
    return;
  }

  if (pid == 0) {
    // 构造参数数组：将找到的 path 放在最后一个参数
    eargv[eargc] = path;
    eargv[eargc + 1] = 0;
    exec(eargv[0], eargv);
    fprintf(2, "find: exec %s failed\n", eargv[0]);
    exit(1);
  }
  else {
    wait(0);
  }
}

void
find(char* path, char* target)
{
  char buf[512];
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type) {
    case T_DEVICE:
    case T_FILE:
      if (strcmp(fmtname(path), target) == 0) {
        run(path);
      }
      break;

    case T_DIR:
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("find: path too long\n");
        break;
      }

      strcpy(buf, path);
      char* p = buf + strlen(buf);
      *p++ = '/';

      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
          continue;

        // de.name 不一定包含 \0，需要先提取出来做比较
        char name[DIRSIZ + 1];
        memmove(name, de.name, DIRSIZ);
        name[DIRSIZ] = 0;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
          continue;

        // 每次循环前彻底清空 p 开始的区域，确保路径字符串干净
        memset(p, 0, DIRSIZ + 1);
        memmove(p, de.name, DIRSIZ);

        find(buf, target);
      }
      break;
  }
  close(fd);
}

int
main(int argc, char* argv[])
{
  int i;

  if (argc < 3) {
    fprintf(2, "Usage: find <path> <filename> [-exec <cmd> ...]\n");
    exit(1);
  }

  if (argc >= 5 && strcmp(argv[3], "-exec") == 0) {
    do_exec = 1;
    eargc = argc - 4;
    for (i = 0; i < eargc; i++) {
      eargv[i] = argv[4 + i];
    }
  }

  find(argv[1], argv[2]);
  exit(0);
}
