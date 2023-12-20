#ifndef _LAMBDARICINT_H
#define _LAMBDARICINT_H
#include "lambdaric.h"
#include "tclstuff.h"
#include "tip445.h"
#include <tclOO.h>
#include <stdlib.h>
#include "obstackpool.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>

#define RX_BUF_SIZE	131072

// Must match with lit_str[] in lambdaric.c
enum {
	L_EMPTY,
	L_TRUE,
	L_FALSE,
	L_APPLY,
	L_ZLIB,
	L_STREAM,
	L_GUNZIP,
	L_DECOMPRESS,
	L_INFLATE,
	L_ADD,
	L_FINALIZE,
	L_RESET,
	L_size
};

struct interp_cx {
	Tcl_Obj*		lit[L_size];
	Tcl_DString		url;
	int				url_base_len;		// Unused?
	struct obstack	ob;
	int				runtime_api_len;
	const char*		runtime_api;
	const char*		api_host;
	const char*		api_port;
	int				sock;
	Tcl_Obj*		handler;
	Tcl_DString		req_id;
	int				responded;
	Tcl_DString		ds_next;
	int				pipeline_depth;
	Tcl_HashTable	transforms;
	uint8_t			rx_buf[RX_BUF_SIZE+1];
	const uint8_t*	lim;
	const uint8_t*	cur;
	const uint8_t*	mar;
	const uint8_t*	tok;

	// HTTP msg fields set for the most recent read_response
	int				http_code;
	Tcl_Obj*		starttime;
	Tcl_DString		context;
	Tcl_Obj*		body;
};

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILD_lambdaric
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif

#define NS	"::lambdaric"

// lambdaric.c internal interface <<<
void register_intrep(Tcl_Obj* obj);
void forget_intrep(Tcl_Obj* obj);
// lambdaric.c internal interface >>>

EXTERN int Lambdaric_Init _ANSI_ARGS_((Tcl_Interp * interp));

#ifdef __cplusplus
}
#endif

#endif // _LAMBDARICINT_H
// vim: foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
