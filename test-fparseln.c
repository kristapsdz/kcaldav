#include <stdio.h>
#include <util.h>

int
main(void)
{
	size_t	 line, len;
	len = line = 0;
	fparseln(NULL, &len, &line, NULL, 0);
	return(0);
}
