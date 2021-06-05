#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user.h"

int test1(void)
{
	printf("\nstarting test 1\n");
	if(fork())
	{
		// WAIT POMEREN ISPOD shm_open
		int fd = shm_open("/test1");
		wait();
		// printf("\first fd %d\n", fd);
		int size = shm_trunc(fd, 400);
		int *p;
		shm_map(fd, (void **) &p, O_RDWR);
		// printf("\nfirst fork %d\n", p);
		sleep(50);
		if(p[0] == 42 && p[1] == 42)
		{
			printf("Test 1 OK (if no other errors appeared)\n");
		}
		else
		{
			printf("Test 1 Not OK\n");
			printf("Output %d %d\n", p[0], p[1]);
		}
		shm_close(fd);
		return 0;
	}

	if(fork())
	{
		int fd = shm_open("/test1");
		// printf("\nsecond fd %d\n", fd);
		int size = shm_trunc(fd, 400);
		int *p;
		shm_map(fd, (void **) &p, O_RDWR);
		// printf("\nsecond fork %d\n", p);
		p[0] = 42;
		shm_close(fd);
		wait();		
	}
	else
	{
		int fd = shm_open("/test1");
		// printf("\else fd %d\n", fd);
		int size = shm_trunc(fd, 400);
		int *p;
		shm_map(fd, (void **) &p, O_RDWR);
		// printf("\nelse fork %d\n", p);
		p[1] = 42;
		shm_close(fd);	
	}
	return 1;
}

int test2(void)
{
	printf("\nstarting test 2\n");
	int fd = shm_open("/test2");
	int size = shm_trunc(fd, 400);
	int *p;
	shm_map(fd, (void **) &p, O_RDWR);
	if(fork())
	{
		wait();
		if(p[0] == 42 && p[1] == 42)
		{
			printf("Test 2 OK (if no other errors appeared)\n");
		} 
		shm_close(fd);
		return 0;
	}

	if(fork())
	{
		p[0] = 42;
		shm_close(fd);
		wait();
	}
	else
	{
		p[1] = 42;
		shm_close(fd);
		
	}
	return 1;
}

int test3(void)
{
	printf("\nstarting test 3\n");
	int fd = shm_open("/test3");
	int pid;
	int size = shm_trunc(fd, 400);
	int *p;
	shm_map(fd, (void **) &p, O_RDONLY);
	if(pid = fork())
	{
		wait();
		printf("Test 3 OK (if trap 14 was triggered before this by proces with pid: %d)\n", pid);
		shm_close(fd);
		return 0;
	}

	printf("Triggering trap 14!\n");
	p[1] = 42;
	shm_close(fd); //this doesent get called, cleanup elsewhere on crash
	return 1;
}

// Same as 3 but writable
int test4(void)
{
	printf("\nstarting test 4\n");
	int fd = shm_open("/test4");
	int pid;
	int size = shm_trunc(fd, 400);
	int *p;
	shm_map(fd, (void **) &p, O_RDWR);
	if(pid = fork())
	{
		wait();
		if (p[1] == 42) {
			printf("Test 4 OK\n", pid);
		} else {
			printf("Test 4 Not OK");
		}
		shm_close(fd);
		return 0;
	}

	p[1] = 42;
	shm_close(fd);
	return 1;
}

int
main(int argc, char *argv[])
{
	if(test1()) goto ex;
	if(test2()) goto ex;
	if(test3()) goto ex;
	if(test4()) goto ex;

ex:
	exit();
}
