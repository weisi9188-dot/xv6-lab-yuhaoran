#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data);

int
main(int argc, char *argv[])
{
  if(argc == 1){
    printf("Example 1:\n");
    int a[2] = { 61810, 2025 };
    memdump("ii", (char*) a);
    
    printf("Example 2:\n");
    memdump("S", "a string");
    
    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *) &s);

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;
    
    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");
    
    printf("Example 4:\n");
    memdump("pihcS", (char*) &example);
    
    printf("Example 5:\n");
    memdump("sccccc", (char*) &example);
  } else if(argc == 2){
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while(n < sizeof(data)){
      int nn = read(0, data + n, sizeof(data) - n);
      if(nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data)
{
  // Your code here.
  for (int i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] == 'i') {
      // 1. 打印 4 字节的 32 位有符号十进制整数
      printf("%d\n", *(int*)data);
      data += 4;
    }
    else if (fmt[i] == 'p') {
      // 2. 打印 8 字节的 64 位十六进制数
      printf("%llx\n", *(unsigned long long*)data);
      data += 8;
    }
    else if (fmt[i] == 'h') {
      // 3. 打印 2 字节的 16 位有符号十进制短整数
      // 注意：xv6 的 printf 不一定支持 %hd，因此需提取到 short 变量中再用 %d 打印
      short val = *(short*)data;
      printf("%d\n", val);
      data += 2;
    }
    else if (fmt[i] == 'c') {
      // 4. 打印 1 字节的 ASCII 字符
      printf("%c\n", *data);
      data += 1;
    }
    else if (fmt[i] == 's') {
      // 5. 数据前 8 字节是一个 64 位指针，指向另一个 C 字符串
      char* str_ptr = *(char**)data;
      printf("%s\n", str_ptr);
      data += 8;
    }
    else if (fmt[i] == 'S') {
      // 6. 剩余的数据直接就是以 '\0' 结尾的本地 C 字符串
      printf("%s\n", data);
      // 由于 'S' 通常用于读取剩余部分，向前移动至字符串末尾之后
      data += strlen(data) + 1;
    }
  }
}
