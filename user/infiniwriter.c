#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	int i = 0;
	for(;;)
	{
		printf("Infiniwriter, tick: %d\n", i++);
		sleep(100);
	}
	exit();
}
