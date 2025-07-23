#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
//C 语言没有内置的 string 类型。它用一种特殊约定的 char 数组来表示我们常说的“字符串”。
void run(char* program, char** args){
    if(fork() == 0){
        exec(program, args);
        exit(0);
    }
    return;
}
//argv: (argument vector) 一个指向字符串的指针数组。
//argv[i] 是地址
int main(int argc, char *argv[]){
//argv：命令加上命令行参数组成的向量 argc：一共有多少个命令行参数
    char buf[2048];
    char* p = buf, * last_p = buf;
    char* argsbuf[128];
    char** args = argsbuf;
    //argv[i] 的地址存入 args 指向的位置
    for(int i = 1; i < argc; i++){
        *args = argv[i];//argsbuf里面的值也变了
        args++;
    }
    char** pa = args;
    //文件描述符，0 代表标准输入 (stdin)。
    //在默认情况下，它连接到你的键盘。但当使用管道 | 时，它会连接到前一个命令的标准输出。
    //当读到文件结尾(EOF)时返回0。
    while(read(0, p, 1) != 0){
        if(*p == ' ' || *p == '\n'){
            //将空格或换行符替换为字符串结束符 \0。
            //这样，从 last_p 到 p 之间的内存区域就成了一个合法的、
            //可以被 exec 等函数识别的C风格字符串。
            *p = '\0';
            //*pa = last_p: 将刚刚形成的这个单词的起始地址 (last_p) 存入 argsbuf 中 pa 指向的位置。
            //pa++: 移动 pa 指针，使其指向 argsbuf 的下一个空位，为下一个参数做准备。
            *(pa++) = last_p;
            last_p = p + 1;
            if(*p == '\n'){
                //exec 命令需要一个以空指针 NULL (也就是 0) 结尾的参数数组。
                *pa = 0;
                run(argv[1], argsbuf);
                pa = args;//复位到所有“初始参数”的末尾。
            }
        }
        p++;
    }
    if(pa != args){
        *p = '\0';
        *(pa++) = last_p;
        *pa = 0;
        run(argv[1], argsbuf);
    }
    while(wait(0) != -1) {};
    exit(0);
}
