#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 定义题目指定的分隔符常量字符串
const char delims[] = " -\r\t\n./,\"";

// 检查字符是否为分隔符
int is_delim(char c) {
  return strchr(delims, c) != 0;
}

void sixfive(int fd) {
  char ch;
  char buf[32]; // 假设单个数字字符长度不超过 31
  int buf_idx = 0;

  // 一次读取一个字符，直到文件结束
  while (read(fd, &ch, 1) > 0) {
    if (is_delim(ch)) {
      // 如果遇到分隔符且缓冲区有数字，处理该数字
      if (buf_idx > 0) {
        buf[buf_idx] = '\0'; // 闭合字符串
        int num = atoi(buf);

        // 检查是否为 5 或 6 的倍数
        if (num % 5 == 0 || num % 6 == 0) {
          printf("%d\n", num);
        }
        buf_idx = 0; // 重置缓冲区
      }
    }
    else {
      // 确保只收集有效的十进制数字字符，防止 xv6 中的 "xv6" 等干扰
      if (ch >= '0' && ch <= '9') {
        if (buf_idx < sizeof(buf) - 1) {
          buf[buf_idx++] = ch;
        }
      }
      else {
        // 如果遇到非数字也非分隔符的字母（如 "xv6" 中的 'x' 和 'v'），
        // 隐式视作切断当前数字，清空缓冲区，保证 "xv6" 中的 "6" 独立出来
        buf_idx = 0;
      }
    }
  }

  // 文件结尾（隐式分隔符）处理：检查最后缓冲区是否还有残留数字
  if (buf_idx > 0) {
    buf[buf_idx] = '\0';
    int num = atoi(buf);
    if (num % 5 == 0 || num % 6 == 0) {
      printf("%d\n", num);
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(2, "Usage: sixfive <filename> ...\n");
    exit(1);
  }
  for (int i = 1; i < argc; i++) {
    int fd = open(argv[i], 0);
    if (fd < 0) {
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      exit(1);
    }
    sixfive(fd);
    close(fd);
  }
  exit(0);
}
