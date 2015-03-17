#include <sys/stat.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

int
main(int argc, char *argv[])
{
	int		 fd, c;
	struct stat	 st;
	size_t		 sz;
	char		*map, *buf;
	struct ical	*p = NULL;

	if (-1 != (c = getopt(argc, argv, "")))
		return(EXIT_FAILURE);

	argc -= optind;
	argv += optind;

	if (0 == argc)
		return(EXIT_FAILURE);

	if (-1 == (fd = open(argv[0], O_RDONLY, 0))) {
		perror(argv[0]);
		return(EXIT_FAILURE);
	} else if (-1 == fstat(fd, &st)) {
		perror(argv[0]);
		close(fd);
		return(EXIT_FAILURE);
	} 

	sz = st.st_size;
	map = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (MAP_FAILED == map) {
		perror(argv[0]);
		return(EXIT_FAILURE);
	} 

	buf = malloc(sz + 1);
	memcpy(buf, map, sz);
	buf[sz] = '\0';
	munmap(map, sz);
	
	if (NULL != (p = ical_parse(buf)))
		ical_printfile(stdout, p);
	ical_free(p);
	free(buf);
	return(NULL == p ? EXIT_FAILURE : EXIT_SUCCESS);
}
