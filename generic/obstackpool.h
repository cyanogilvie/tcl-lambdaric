#ifndef _OBSTACKPOOL_H
#define _OBSTACKPOOL_H
#define obstack_chunk_alloc	malloc
#define obstack_chunk_free	free
#include <obstack.h>
#include <stdint.h>
#include <tcl.h>

enum obstack_pool_estimate {
	OBSTACK_POOL_SMALL,			// A few pages
	OBSTACK_POOL_MEDIUM			// A MB or more
};

struct obstack* obstack_pool_get(enum obstack_pool_estimate est);
void obstack_pool_release(struct obstack* ob);
void obstack_pool_groom(int64_t ts);
int obstack_pool_init(Tcl_Interp* interp);
int obstack_pool_fini(Tcl_Interp* interp);

#endif
