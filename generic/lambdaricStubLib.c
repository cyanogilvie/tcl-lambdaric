#undef USE_TCL_STUBS
#undef USE_LAMBDARIC_STUBS
#define USE_TCL_STUBS 1
#define USE_LAMBDARIC_STUBS 1

#include "lambdaric.h"

MODULE_SCOPE const LambdaricStubs*	lambdaricStubsPtr;
const LambdaricStubs*					lambdaricStubsPtr = NULL;

#undef LambdaricInitializeStubs
MODULE_SCOPE const char* LambdaricInitializeStubs(Tcl_Interp* interp)
{
	const char*	got = NULL;
	fprintf(stderr, "In LambdaricInitializeStubs, verion: (%s)\n", PACKAGE_VERSION);
	got = Tcl_PkgRequireEx(interp, PACKAGE_NAME, PACKAGE_VERSION, 0, &lambdaricStubsPtr);
	fprintf(stderr, "Got ver: (%s), lambdaricStubsPtr: %p\n", got, lambdaricStubsPtr);
	return got;
}
