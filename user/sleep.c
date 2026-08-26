#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
  // 检查命令行参数是否少于 2 个（程序名本身算 1 个，后面必须跟时间参数）
  if (argc < 2) {
    fprintf(2, "Usage: sleep <ticks>\n");
    exit(1);
  }

  // 将字符串参数转换为整数节拍数
  int ticks = atoi(argv[1]);

  // 调用 xv6 系统调用 sleep() 暂停指定的节拍数
  // 注意：题目中提示叫 pause()，但在标准 xv6 中该系统调用函数名为 sleep()
  if (pause(ticks) < 0) {
    fprintf(2, "sleep: system call failed\n");
    exit(1);
  }

  // 正常退出程序
  exit(0);
}
