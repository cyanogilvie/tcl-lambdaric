library lambdaric
interface lambdaric

declare 0 generic {
	int Lambdaric_SendResponsev(Tcl_Interp* interp, const char* tail, struct iovec* body, int iovcnt)
}
declare 1 generic {
	int Lambdaric_SendResponse(Tcl_Interp* interp, const char* tail, const void* body, int bodylen)
}

# vim: ft=tcl foldmethod=marker foldmarker=<<<,>>>
