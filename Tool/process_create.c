#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include "process_create.h"

pid_t process_pid;
int process_create(){
 // 用来存 fork() 的返回值（区分父子进程）

    process_pid = fork();
     if (process_pid) {
        perror("fork");   // 打印错误原因
        return -1;
    }
    return process_pid;
}




    
