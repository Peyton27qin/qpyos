#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
//筛选质数的函数，用一个管道作为参数
void sieve(int pleft[2]){
    //从左邻居读取整数
    int p;
    read(pleft[0], &p, sizeof(p));
    if(p == -1){
        exit(0);   //如果读到-1，表示结束，退出进程//左退出
    }
    printf("prime %d\n", p); //此时接收到的数字肯定是质数
    //创建一个新的管道
    int pright[2];
    pipe(pright);
    if(fork() == 0){		//右邻居 子进程
        close(pright[1]); 	//右邻居用不到这个管道的写端口，关闭
        close(pleft[0]); 	//左邻居用不到这个管道的读端口，关闭
        sieve(pright);    	//递归调用筛选函数
    } else{					//父进程
        close(pright[0]); 	//当前进程用不到这个管道的读端，关闭
        //从左邻居接收数字
        int buf;
        while(read(pleft[0], &buf, sizeof(buf)) && buf != -1){
            if(buf % p != 0){ //若接收到的数字不是第一次接收到的数字的倍数
                write(pright[1], &buf, sizeof(buf));//则往右邻居写入这个数字
            }
        }
        //此时接收到了左邻居传来的-1，要给右邻居也传-1，结束右邻居进程
        buf = -1;
        write(pright[1], &buf, sizeof(buf));
        wait(0);
        exit(0);//右退出
    }
}
int main(int argc, char **argv){
    //创建初始管道
    int input_pipe[2];
    pipe(input_pipe);
    if(fork() == 0){ 			//右邻居  子进程
        close(input_pipe[1]);	//右邻居用不到这个管道的写端，关闭
        sieve(input_pipe);		//调用筛选函数
        exit(0);
    } else{
        close(input_pipe[0]);  	//父进程
        int i;					//父进程只会给右管道写数据，关闭读端
        for(i = 2; i < 35; i++){
            write(input_pipe[1], &i, sizeof(i));
        }
        //结束符号
        i = -1;
        write(input_pipe[1], &i, sizeof(i));
    }
    wait(0);//等待子进程结束
    //注意：这里无法等待子进程的子进程，只能直接等待子进程
    //在sieve（）中各自执行wait（0），形成等待链
    exit(0);
    
}

