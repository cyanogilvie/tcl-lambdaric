#include "lambdaric.h"

/* !BEGIN!: Do not edit below this line. */

const LambdaricStubs lambdaricStubs = {
    TCL_STUB_MAGIC,
    0,
    Lambdaric_SendResponsev, /* 0 */
    Lambdaric_SendResponse, /* 1 */
};

/* !END!: Do not edit above this line. */


const LambdaricStubs* LambdaricStubsPtr = NULL;
MODULE_SCOPE const LambdaricStubs* const lambdaricConstStubsPtr;
const LambdaricStubs* const lambdaricConstStubsPtr = &lambdaricStubs;
