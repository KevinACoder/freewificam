#ifndef _LINUXKPI_LINUX_SEQ_FILE_H_
#define _LINUXKPI_LINUX_SEQ_FILE_H_
/* [S6] B8/C-tier: procfs is eliminated, but the vendor's dump helpers are
 * written against the seq_file face (RTW_PRINT_SEL -> _seqdump -> seq_printf
 * on PLATFORM_LINUX).  Shapes + the printf family live here; the dump
 * paths are dead in W1-W5 and the functions land on the console sink. */
#include "../../linuxkpi_core.h"

struct seq_file {
	char	*buf;
	size_t	size;
	size_t	from;
	size_t	count;
	loff_t	index;
	loff_t	read_pos;
	void	*private;
};

int seq_printf(struct seq_file *m, const char *fmt, ...)
	__attribute__((__format__(printf, 2, 3)));
int seq_puts(struct seq_file *m, const char *str);
int seq_putc(struct seq_file *m, char c);
#endif
