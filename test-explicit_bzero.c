#include <string.h>

int
main(void)
{
	char	foo[32];

	foo[0] = 'a';
	explicit_bzero(foo, sizeof(foo));
	return(0);
}
