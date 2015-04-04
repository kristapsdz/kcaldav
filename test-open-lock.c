#include <fcntl.h>
#include <unistd.h>

int
main(void)
{
	int	 fd;

	if (-1 == (fd = open(".", O_RDONLY | O_SHLOCK, 0)))
		return(1);
	close(fd);
	return(0);
}
