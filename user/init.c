// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fcntl.h"

char *argv1[] = { "1", 0 };
char *argv2[] = { "2", 0 };
char *argv3[] = { "3", 0 };
char *argv4[] = { "4", 0 };
char *argv5[] = { "5", 0 };
char *argv6[] = { "6", 0 };

int
main(void)
{
	int pid, wpid;

	if(getpid() != 1){
		fprintf(2, "init: already running\n");
		exit();
	}

	for(;;){
		printf("init: starting sh\n");
		
		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv1);
			printf("init: exec sh failed\n");
			exit();
		}

		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv2);
			printf("init: exec sh failed\n");
			exit();
		}

		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv3);
			printf("init: exec sh failed\n");
			exit();
		}

		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv4);
			printf("init: exec sh failed\n");
			exit();
		}

		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv5);
			printf("init: exec sh failed\n");
			exit();
		}

		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv6);
			printf("init: exec sh failed\n");
			exit();
		}

		while((wpid=wait()) >= 0 && wpid != pid)
			printf("zombie!\n");
	}
}
