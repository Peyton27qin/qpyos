//user/sleep.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv){ // 第一个参数是命令行参数的总数目（包括程序名）
                                //第二个是储存每个参数的字符串数组
	if(argc < 2){
		printf("usage: sleep <ticks>\n");
	}
	sleep(atoi(argv[1]));
	exit(0);
}
