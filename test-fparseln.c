#include <paths.h>
#include <stdio.h>
#include <util.h>

int
main(void)
{
	size_t	 line, len;
	FILE	*f;

	f = fopen(_PATH_DEVNULL, "r");
	len = line = 0;
	fparseln(f, &len, &line, NULL, 0);
	return(0);
}
