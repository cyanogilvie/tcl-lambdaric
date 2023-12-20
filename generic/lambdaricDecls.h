#include <sys/uio.h>

/* !BEGIN!: Do not edit below this line. */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exported function declarations:
 */

/* 0 */
EXTERN int		Lambdaric_SendResponsev(Tcl_Interp*interp,
				const char*tail, struct iovec*body,
				int iovcnt);
/* 1 */
EXTERN int		Lambdaric_SendResponse(Tcl_Interp*interp,
				const char*tail, const void*body,
				int bodylen);

typedef struct LambdaricStubs {
    int magic;
    void *hooks;

    int (*lambdaric_SendResponsev) (Tcl_Interp*interp, const char*tail, struct iovec*body, int iovcnt); /* 0 */
    int (*lambdaric_SendResponse) (Tcl_Interp*interp, const char*tail, const void*body, int bodylen); /* 1 */
} LambdaricStubs;

extern const LambdaricStubs *lambdaricStubsPtr;

#ifdef __cplusplus
}
#endif

#if defined(USE_LAMBDARIC_STUBS)

/*
 * Inline function declarations:
 */

#define Lambdaric_SendResponsev \
	(lambdaricStubsPtr->lambdaric_SendResponsev) /* 0 */
#define Lambdaric_SendResponse \
	(lambdaricStubsPtr->lambdaric_SendResponse) /* 1 */

#endif /* defined(USE_LAMBDARIC_STUBS) */

/* !END!: Do not edit above this line. */
