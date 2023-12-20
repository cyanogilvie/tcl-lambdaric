#include "lambdaricInt.h"

// Must be kept in sync with the enum in lambdaricInt.tcl
static const char* lit_str[L_size] = {
	"",				// L_EMPTY
	"1",			// L_TRUE
	"0",			// L_FALSE
	"apply",		// L_APPLY,
	"zlib",			// L_ZLIB,
	"stream",		// L_STREAM,
	"gunzip",		// L_GUNZIP,
	"decompress",	// L_DECOMPRESS,
	"inflate",		// L_INFLATE,
	"add",			// L_ADD,
	"-finalize",	// L_FINALIZE,
	"reset",		// L_RESET,
};

TCL_DECLARE_MUTEX(g_intreps_mutex);
int				g_intreps_refcount = 0;
Tcl_HashTable	g_intreps;

TCL_DECLARE_MUTEX(g_config_mutex);
Tcl_Obj*		g_packagedir = NULL;
Tcl_Obj*		g_includedir = NULL;
int				g_config_refcount = 0;
static Tcl_Config cfg[] = {
	{"libdir,runtime",			LAMBDARIC_LIBRARY_PATH_INSTALL},	// Overwritten by init, must be first
	{"includedir,runtime",		LAMBDARIC_INCLUDE_PATH_INSTALL},	// Overwritten by init, must be second
	{"packagedir,runtime",		LAMBDARIC_PACKAGE_PATH_INSTALL},	// Overwritten by init, must be third
	{"libdir,install",			LAMBDARIC_LIBRARY_PATH_INSTALL},
	{"includedir,install",		LAMBDARIC_INCLUDE_PATH_INSTALL},
	{"packagedir,install",		LAMBDARIC_PACKAGE_PATH_INSTALL},
	{"library",					LAMBDARIC_LIBRARY},
	{"stublib",					LAMBDARIC_STUBLIB},
	{"header",					LAMBDARIC_HEADER},
	{NULL, NULL}
};

// Internal API <<<
void free_interp_cx(ClientData cdata, Tcl_Interp* interp) //<<<
{
	struct interp_cx*	l = (struct interp_cx*)cdata;

	for (int i=0; i<L_size; i++) replace_tclobj(&l->lit[i], NULL);

	if (l->sock != -1) {
		//fprintf(stderr, "Disconnecting: %d\n", l->sock);
		close(l->sock);
		l->sock = -1;
	}
	Tcl_DStringFree(&l->url);
	Tcl_DStringFree(&l->req_id);
	Tcl_DStringFree(&l->ds_next);
	replace_tclobj(&l->handler,	NULL);

	Tcl_HashEntry*	he = NULL;
	Tcl_HashSearch	search;
	for (he = Tcl_FirstHashEntry(&l->transforms, &search); he; he = Tcl_NextHashEntry(&search)) {
		Tcl_Obj* stack = Tcl_GetHashValue(he);
		if (!stack) {
			fprintf(stderr, "RELEASE, no stream stack stored for (%s)\n", (const char*)Tcl_GetHashKey(&l->transforms, he));
			continue;
		}

		replace_tclobj(&stack, NULL);
	}
	Tcl_DeleteHashTable(&l->transforms);

	replace_tclobj(&l->body, NULL);
	replace_tclobj(&l->starttime, NULL);

	// Must be last
	obstack_free(&l->ob, NULL);

	ckfree(l);
}

//>>>
void register_intrep(Tcl_Obj* obj) //<<<
{
	Tcl_HashEntry*		he = NULL;
	int					new = 0;

	Tcl_MutexLock(&g_intreps_mutex);
	he = Tcl_CreateHashEntry(&g_intreps, obj, &new);
	if (!new) Tcl_Panic("register_intrep: already registered");
	Tcl_SetHashValue(he, obj);
	Tcl_MutexUnlock(&g_intreps_mutex);
}

//>>>
void forget_intrep(Tcl_Obj* obj) //<<<
{
	Tcl_HashEntry*		he = NULL;

	Tcl_MutexLock(&g_intreps_mutex);
	he = Tcl_FindHashEntry(&g_intreps, obj);
	if (!he) Tcl_Panic("forget_intrep: not registered");
	Tcl_DeleteHashEntry(he);
	Tcl_MutexUnlock(&g_intreps_mutex);
}

//>>>

/*!rules:re2c:common //<<<
	re2c:api:style			= free-form;
	re2c:yyfill:enable		= 0;
	re2c:define:YYCTYPE		= uint8_t;

	end = "\x00";
	eol = "\r"? "\n";
	//eol = "\r" "\n";
	//eol = "\n";

	crlf        = eol;
	sp          = " ";
	htab        = "\t";
	ows         = (sp | htab)*;
	digit       = [0-9];
	alpha       = [a-zA-Z];
	hexdigit    = [0-9a-fA-F];
	unreserved  = alpha | digit | [-._~];
	pct_encoded = "%" hexdigit{2};
	sub_delims  = [!$&'()*+,;=];
	pchar       = unreserved | pct_encoded | sub_delims | [:@];
	vchar       = [\x1f-\x7e];
	tchar       = [-!#$%&'*+.^_`|~] | digit | alpha;

	obs_fold       = crlf (sp | htab)+;
	obs_text       = [\x80-\xff];
	field_name     = tchar+;
	field_vchar    = vchar | obs_text;
	field_content  = field_vchar ((sp | htab)+ field_vchar)?;
	field_value    = (field_content | obs_fold)*;
	header_field   = #h1 field_name #h2 ":" ows #h3 field_value #h4 ows #h5;
	scheme         = alpha (alpha | digit | [-+.])*;
	userinfo       = (unreserved | pct_encoded | sub_delims | ":")*;
	dec_octet
		= digit
		| [\x31-\x39] digit
		| "1" digit{2}
		| "2" [\x30-\x34] digit
		| "25" [\x30-\x35];
	ipv4address    = dec_octet "." dec_octet "." dec_octet "." dec_octet;
	h16            = hexdigit{1,4};
	ls32           = h16 ":" h16 | ipv4address;
	ipv6address
		=                            (h16 ":"){6} ls32
		|                       "::" (h16 ":"){5} ls32
		| (               h16)? "::" (h16 ":"){4} ls32
		| ((h16 ":"){0,1} h16)? "::" (h16 ":"){3} ls32
		| ((h16 ":"){0,2} h16)? "::" (h16 ":"){2} ls32
		| ((h16 ":"){0,3} h16)? "::"  h16 ":"     ls32
		| ((h16 ":"){0,4} h16)? "::"              ls32
		| ((h16 ":"){0,5} h16)? "::"              h16
		| ((h16 ":"){0,6} h16)? "::";
	ipvfuture      = "v" hexdigit+ "." (unreserved | sub_delims | ":" )+;
	ip_literal     = "[" ( ipv6address | ipvfuture ) "]";
	reg_name       = (unreserved | pct_encoded | sub_delims)*;
	path_abempty   = ("/" pchar*)*;
	path_absolute  = "/" (pchar+ ("/" pchar*)*)?;
	path_rootless  = pchar+ ("/" pchar*)*;
	path_empty     = "";
	host           = ip_literal | ipv4address | reg_name;
	port           = digit*;
	query          = (pchar | [/?])*;
	absolute_uri   = @s1 scheme @s2 ":"
		( "//" (@u1 userinfo @u2 "@")? @hs1 host @hs2 (":" @r1 port @r2)? @p1 path_abempty @p2
		| @p3 (path_absolute | path_rootless | path_empty) @p4
		) ("?" @q1 query @q2)?;
	authority      = (@u3 userinfo @u4 "@")? @hs3 host @hs4 (":" @r3 port @r4)?;
	origin_form    = @p5 path_abempty @p6 ("?" @q3 query @q4)?;
	http_name      = "HTTP";
	http_version   = http_name "/" digit "." digit;
	request_target
		= @at authority
		| @au absolute_uri
		| @of origin_form
		| "*";
	token          = tchar+;
	method         = token;
	request_line   = @m1 method @m2 sp request_target sp @v3 http_version @v4 crlf;
	status_code    = digit{3};
	reason_phrase  = (htab | sp | vchar | obs_text)*;
	status_line    = @v1 http_version @v2 sp @st1 status_code @st2 sp @rp1 reason_phrase @rp2 crlf;
	start_line     = (request_line | status_line);
	message_head   = start_line (header_field crlf)* crlf;
	qdtext
		= htab
		| sp
		| [\x21-\x7E] \ '"' \ "\\"
		| obs_text;
	quoted_pair    = "\\" ( htab | sp | vchar | obs_text );
	quoted_string  = '"' ( qdtext | quoted_pair )* '"';
*/ //>>>

static int parse_runtime_api(Tcl_Interp* interp, const uint8_t* api) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	const uint8_t*		YYCURSOR = api;
	const uint8_t*		YYMARKER;
	const uint8_t		*he, *ps, *pe;
	/*!stags:re2c:parse_runtime_api format = "const uint8_t *@@{tag}; "; */

	/*!local:re2c:parse_runtime_api
		!use:common;
		re2c:tags	= 1;

		hostchar	= [^] \ end \ ":";

		hostchar+ @he (":" @ps digit+)? @pe end	{
			l->runtime_api_len  = pe-api;
			l->runtime_api      = obstack_copy0(&l->ob, api, l->runtime_api_len);
			l->api_host         = obstack_copy0(&l->ob, api, he-api);
			if (ps) l->api_port = obstack_copy0(&l->ob, ps,  pe-ps);
			goto finally;
		}

		*	{ THROW_ERROR_LABEL(finally, code, "Could not parse runtime_api: \"", api, "\""); }
	*/

finally:
	return code;
}

//>>>
static int connect_runtime_api(Tcl_Interp* interp, const char* host, const char* port, int* sock) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	struct addrinfo		*result = NULL;
	struct addrinfo		*rp = NULL;
	int					rc, fd=-1;

	struct addrinfo		hints = {
		.ai_family		= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM, 
		.ai_protocol	= IPPROTO_TCP
	};

	if (host[0] == '/') { // Local socket (unix domain socket)
		struct sockaddr_un	local = {.sun_family = AF_UNIX};
		fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd == -1)
			THROW_POSIX_LABEL(finally, code, "Could not create local socket");

		strncpy(local.sun_path, host, 107);
		if (connect(fd, (struct sockaddr*)&local, sizeof(local)))
			THROW_POSIX_LABEL(finally, code, "Could not connect to runtime api");
	} else {
		if ((rc = getaddrinfo(host, port, &hints, &result)))
			THROW_ERROR_LABEL(finally, code, "Could not resolve runtime api \"", l->runtime_api, "\": ", gai_strerror(rc));
		for (rp=result; rp; rp=rp->ai_next) {
			fd = socket(rp->ai_family, rp->ai_socktype | SOCK_CLOEXEC, rp->ai_protocol);
			if (fd == -1) continue;
			if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1) break;
			close(fd);
		}

		freeaddrinfo(result);

		if (rp == NULL)
			THROW_ERROR_LABEL(finally, code, "Could not connect to runtime api \"", l->runtime_api, "\"");
	}

	*sock = fd;
	fd = -1;

	l->cur = l->mar = l->tok = l->lim = l->rx_buf + RX_BUF_SIZE;

finally:
	if (fd != -1) close(fd);
	return code;
}

//>>>
static void path_encode(Tcl_DString* ds, const char* element) //<<<
{
	const uint8_t*	YYCURSOR = (const uint8_t*)element;
	const uint8_t*	tok;
	uint8_t			buf[3] = "%xx";

loop:
	tok = YYCURSOR;
	/*!local:re2c:path_encode
		!use:common;

		allowed		= unreserved | sub_delims | [:@];
		cesu8null	= "\xc0\x80";

		allowed+	{ Tcl_DStringAppend(ds, (const char*)tok, YYCURSOR-tok);	goto loop; }
		cesu8null	{ Tcl_DStringAppend(ds, "%00", 3);							goto loop; }
		end			{ return; }
		*	{
			const uint8_t	c = (uint8_t)YYCURSOR[-1];
			const uint8_t	h = c >> 4;
			const uint8_t	l = c & 0xF;
			buf[1] = h<10 ? '0'+h : 'a'+h-10;
			buf[2] = l<10 ? '0'+l : 'a'+l-10;
			Tcl_DStringAppend(ds, (const char*)buf, 3);
			goto loop;
		}
	*/
}

//>>>
static int write_iovecs(Tcl_Interp* interp, struct iovec* vecs, int count) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	int					vecs_remain = count;

	while (vecs_remain) {
		//ssize_t r = writev(l->sock, vecs, vecs_remain);
		ssize_t r = sendmsg(l->sock, &(struct msghdr){
			.msg_iov	= vecs,
			.msg_iovlen	= vecs_remain
		}, 0);

		if (r == -1) {
			if (errno == EINTR) continue;
			THROW_POSIX_LABEL(finally, code, "Could not write to lambda runtime api socket");
		}
		while (vecs_remain && r >= vecs->iov_len) {
			r -= vecs->iov_len;
			vecs++;
			vecs_remain--;
		}
		if (vecs_remain) {
			vecs->iov_base += r;
			vecs->iov_len -= r;
		}
	}

finally:
	return code;
}

//>>>
static int send_req(Tcl_Interp* interp, const char* method, int method_len, Tcl_DString* endpoint, const void* body, int bodylen) //<<<
{
	#define PART(name, str, name_len) static char* name = str; static const int name_len = sizeof(str)-1
	PART(tail, " HTTP/1.1\r\nHost: ",	tail_len);
	PART(cl,	"\r\nContent-Length: ",	cl_len);
	PART(hend,	"\r\n\r\n",				hend_len);
	#undef PART
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	const size_t		bodybytes = bodylen == -1 ? strlen(body) : bodylen;

	if (bodybytes) {
		char		lenstr[22];	// (int)(log(pow(2,((sizeof(size_t)) * 8))-1)/log(10))+1 == 20, +1 for sign +1 for null term
		int			lenlen;

		if (bodylen < 10) {
			lenlen = 1;
			lenstr[0] = bodylen+'0';
			lenstr[1] = 0;
		} else if (bodylen < 100) {
			lenlen = 2;
			lenstr[0] = (bodylen/10)+'0';
			lenstr[1] = (bodylen%10)+'0';
			lenstr[2] = 0;
		} else if (bodylen < 1000) {
			lenlen = 3;
			lenstr[0] =  (bodylen/100)     +'0';
			lenstr[1] = ((bodylen/ 10)%10) +'0';
			lenstr[2] =  (bodylen     %10) +'0';
			lenstr[3] = 0;
		} else if (bodylen < 10000) {
			lenlen = 4;
			lenstr[0] =  (bodylen/1000)     +'0';
			lenstr[1] = ((bodylen/ 100)%10) +'0';
			lenstr[2] = ((bodylen/  10)%10) +'0';
			lenstr[3] =  (bodylen      %10) +'0';
			lenstr[4] = 0;
		} else if (bodylen < 100000) {
			lenlen = 5;
			lenstr[0] =  (bodylen/10000)     +'0';
			lenstr[1] = ((bodylen/ 1000)%10) +'0';
			lenstr[2] = ((bodylen/  100)%10) +'0';
			lenstr[3] = ((bodylen/   10)%10) +'0';
			lenstr[4] =  (bodylen       %10) +'0';
			lenstr[5] = 0;
		} else if (bodylen < 1000000) {
			lenlen = 6;
			lenstr[0] =  (bodylen/100000)     +'0';
			lenstr[1] = ((bodylen/ 10000)%10) +'0';
			lenstr[2] = ((bodylen/  1000)%10) +'0';
			lenstr[3] = ((bodylen/   100)%10) +'0';
			lenstr[4] = ((bodylen/    10)%10) +'0';
			lenstr[5] =  (bodylen        %10) +'0';
			lenstr[6] = 0;
		} else {
			lenlen = sprintf(lenstr, "%zd", bodybytes);
		}

		TEST_OK_LABEL(finally, code, write_iovecs(interp, (struct iovec[]){
			{ (void*)method					, method_len					},
			{ " "							, 1								},
			{ Tcl_DStringValue(&l->url)		, Tcl_DStringLength(&l->url)	},
			{ Tcl_DStringValue(endpoint)	, Tcl_DStringLength(endpoint)	},
			{ tail							, tail_len						},
			{ (void*)l->runtime_api			, l->runtime_api_len			},
			{ cl							, cl_len						},
			{ lenstr						, lenlen						},
			{ hend							, hend_len						},
			{ (void*)body					, bodybytes						}
		}, 10));
	} else {
		TEST_OK_LABEL(finally, code, write_iovecs(interp, (struct iovec[]){
			{ (void*)method					, method_len					},
			{ " "							, 1								},
			{ Tcl_DStringValue(&l->url)		, Tcl_DStringLength(&l->url)	},
			{ Tcl_DStringValue(endpoint)	, Tcl_DStringLength(endpoint)	},
			{ tail							, tail_len						},
			{ (void*)l->runtime_api			, l->runtime_api_len			},
			{ hend							, hend_len						}
		}, 7));
	}

	l->pipeline_depth++;

finally:
	return code;
}

//>>>
static int send_reqv(Tcl_Interp* interp, const char* method, int method_len, Tcl_DString* endpoint, struct iovec* body, int iovcnt) //<<<
{
	#define PART(name, str, name_len) static char* name = str; static const int name_len = sizeof(str)-1
	PART(tail, " HTTP/1.1\r\nHost: ",	tail_len);
	PART(cl,	"\r\nContent-Length: ",	cl_len);
	PART(hend,	"\r\n\r\n",				hend_len);
	#undef PART
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	struct obstack*		ob = NULL;

	if (iovcnt) {
		char				lenstr[22];	// (int)(log(pow(2,((sizeof(size_t)) * 8))-1)/log(10))+1 == 20, +1 for sign +1 for null term
		int					lenlen;
		const int			prefix_iovcnt = 9;
		const int			total_iovcnt = prefix_iovcnt+iovcnt;
		size_t				bodybytes = 0;
		ob = obstack_pool_get(OBSTACK_POOL_SMALL);
		struct iovec*const	iov = obstack_alloc(ob, sizeof iov[0] * total_iovcnt);

		for (int i=0; i<iovcnt; i++) {
			struct iovec*	this_iov = &body[i];
			if (this_iov->iov_len == -1)
				this_iov->iov_len = strlen(this_iov->iov_base);

			bodybytes += this_iov->iov_len;
		}

		if (bodybytes < 10) {
			lenlen = 1;
			lenstr[0] = bodybytes+'0';
			lenstr[1] = 0;
		} else if (bodybytes < 100) {
			lenlen = 2;
			lenstr[0] = (bodybytes/10)+'0';
			lenstr[1] = (bodybytes%10)+'0';
			lenstr[2] = 0;
		} else if (bodybytes < 1000) {
			lenlen = 3;
			lenstr[0] =  (bodybytes/100)     +'0';
			lenstr[1] = ((bodybytes/ 10)%10) +'0';
			lenstr[2] =  (bodybytes     %10) +'0';
			lenstr[3] = 0;
		} else if (bodybytes < 10000) {
			lenlen = 4;
			lenstr[0] =  (bodybytes/1000)     +'0';
			lenstr[1] = ((bodybytes/ 100)%10) +'0';
			lenstr[2] = ((bodybytes/  10)%10) +'0';
			lenstr[3] =  (bodybytes      %10) +'0';
			lenstr[4] = 0;
		} else if (bodybytes < 100000) {
			lenlen = 5;
			lenstr[0] =  (bodybytes/10000)     +'0';
			lenstr[1] = ((bodybytes/ 1000)%10) +'0';
			lenstr[2] = ((bodybytes/  100)%10) +'0';
			lenstr[3] = ((bodybytes/   10)%10) +'0';
			lenstr[4] =  (bodybytes       %10) +'0';
			lenstr[5] = 0;
		} else if (bodybytes < 1000000) {
			lenlen = 6;
			lenstr[0] =  (bodybytes/100000)     +'0';
			lenstr[1] = ((bodybytes/ 10000)%10) +'0';
			lenstr[2] = ((bodybytes/  1000)%10) +'0';
			lenstr[3] = ((bodybytes/   100)%10) +'0';
			lenstr[4] = ((bodybytes/    10)%10) +'0';
			lenstr[5] =  (bodybytes        %10) +'0';
			lenstr[6] = 0;
		} else {
			lenlen = sprintf(lenstr, "%zd", bodybytes);
		}

		iov[0] = (struct iovec){ (void*)method				, method_len					};
		iov[1] = (struct iovec){ " "						, 1								};
		iov[2] = (struct iovec){ Tcl_DStringValue(&l->url)	, Tcl_DStringLength(&l->url)	};
		iov[3] = (struct iovec){ Tcl_DStringValue(endpoint)	, Tcl_DStringLength(endpoint)	};
		iov[4] = (struct iovec){ tail						, tail_len						};
		iov[5] = (struct iovec){ (void*)l->runtime_api		, l->runtime_api_len			};
		iov[6] = (struct iovec){ cl							, cl_len						};
		iov[7] = (struct iovec){ lenstr						, lenlen						};
		iov[8] = (struct iovec){ hend						, hend_len						};
		memcpy(&iov[prefix_iovcnt], body, iovcnt*(sizeof iov[0]));

		TEST_OK_LABEL(finally, code, write_iovecs(interp, iov, total_iovcnt));
	} else {
		TEST_OK_LABEL(finally, code, write_iovecs(interp, (struct iovec[]){
			{ (void*)method					, method_len					},
			{ " "							, 1								},
			{ Tcl_DStringValue(&l->url)		, Tcl_DStringLength(&l->url)	},
			{ Tcl_DStringValue(endpoint)	, Tcl_DStringLength(endpoint)	},
			{ tail							, tail_len						},
			{ (void*)l->runtime_api			, l->runtime_api_len			},
			{ hend							, hend_len						}
		}, 7));
	}

	l->pipeline_depth++;

finally:
	if (ob) obstack_pool_release(ob);
	return code;
}

//>>>
static int fill(Tcl_Interp* interp, int* code) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	const size_t		shift = l->rx_buf - l->tok;
	const size_t		used  = l->lim - l->tok;
	int					r;

	//fprintf(stderr, "fill\n");
	// Error - lexeme too long to fit in RX_BUF_SIZE
	if (shift < 1) return 2;

	memmove(l->rx_buf, l->tok, used);
	l->lim += shift;
	l->cur += shift;
	l->mar += shift;
	l->tok += shift;

retry:
	//fprintf(stderr, "fill reading, asking for (%d)\n", RX_BUF_SIZE - used);
	r = read(l->sock, (uint8_t*)l->lim, RX_BUF_SIZE - used);
	//fprintf(stderr, "fill got %d\n", r);
	if (r == -1) {
		if (errno == EINTR) goto retry;
		THROW_POSIX_LABEL(err, *code, "Could not read from lambda runtime api socket");
	} else if (r == 0) {
		// EOF
		return 1;
	}
	l->lim += r;
	*(uint8_t*)&l->lim[0] = 0;
	//fprintf(stderr, "fill returning, avail: %d\n", l->lim - l->cur);
	return 0;

err:
	return 1;
}

//>>>
static void append_json_string(Tcl_DString* ds, uint8_t* str, int len) //<<<
{
	const uint8_t*	YYCURSOR = str;
	const uint8_t*	YYLIMIT = str+len;
	const uint8_t*	YYMARKER;
	const uint8_t*	tok;

	const uint8_t	old_end = str[len];
	str[len] = 0;
	Tcl_DStringAppend(ds, "\"", 1);
	for (;;) {
		tok = YYCURSOR;
		/*!local:re2c:append_json_string
			re2c:yyfill:enable	= 0;
			re2c:encoding:utf8	= 1;
			re2c:define:YYCTYPE	= uint8_t;
			re2c:eof			= 0;

			control		= [\x00-\x1F\x7F];
			backslash	= "\x5c";
			dquote		= "\x22";
			unquoted	= [^] \ control \ dquote \ backslash;

			unquoted+	{ Tcl_DStringAppend(ds, (const char*)tok, YYCURSOR-tok);	continue; }
			"\b"		{ Tcl_DStringAppend(ds, "\\b", 2);							continue; }
			"\f"		{ Tcl_DStringAppend(ds, "\\f", 2);							continue; }
			"\n"		{ Tcl_DStringAppend(ds, "\\n", 2);							continue; }
			"\r"		{ Tcl_DStringAppend(ds, "\\r", 2);							continue; }
			"\t"		{ Tcl_DStringAppend(ds, "\\t", 2);							continue; }
			"\x00"		{ Tcl_DStringAppend(ds, "\\u0000", 6);						continue; }
			$			{ Tcl_DStringAppend(ds, "\"",  1);							goto finally; }

			* {
				Tcl_DStringAppend(ds, (tok[0]==0xc0 && tok[1]==0x80) ? "\\u0000" : "\\ufffd", 6);
				continue;
			}
		*/
	}

finally:
	str[len] = old_end;
}

//>>>
static void append_json_obj_key(Tcl_DString* ds, const uint8_t* key, size_t keylen, const uint8_t* val, size_t vallen) //<<<
{
	if (Tcl_DStringValue(ds)[Tcl_DStringLength(ds)-1] != 0x7b) Tcl_DStringAppend(ds, ",", 1);
	append_json_string(ds, (uint8_t*)key, keylen);
	Tcl_DStringAppend(ds, ":", 1);
	append_json_string(ds, (uint8_t*)val, vallen);
}

//>>>
static int get_transform(Tcl_Interp* interp, Tcl_DString* te, Tcl_Obj** streamcmd) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);

	if (Tcl_DStringLength(te) == 1) {
		// No transformation needed, just chunk encoded
		replace_tclobj(streamcmd, NULL);
		return TCL_OK;
	}

	int				code = TCL_OK;
	int				isnew;
	Tcl_HashEntry*	he = Tcl_CreateHashEntry(&l->transforms, Tcl_DStringValue(te), &isnew);
	const int		cmdwords = 3;
	Tcl_Obj*		create_stream_cmd[3] = {0};
	Tcl_Obj*		stack = NULL;

	if (isnew) {
		replace_tclobj(&stack, Tcl_NewListObj(Tcl_DStringLength(te)-1, NULL));

		replace_tclobj(&create_stream_cmd[0], l->lit[L_ZLIB]);
		replace_tclobj(&create_stream_cmd[1], l->lit[L_STREAM]);

		const char*	encodings = Tcl_DStringValue(te);
		const char*	e = encodings + Tcl_DStringLength(te)-1;

		for (; e >= encodings; e--) {
			Tcl_Obj*	mode = NULL;

			switch (*e) {
				case 'C': continue;
				case 'g': mode = l->lit[L_GUNZIP];		break;
				case 'd': mode = l->lit[L_INFLATE];		break;
				case 'c': mode = l->lit[L_DECOMPRESS];	break;
				default: THROW_ERROR_LABEL(finally, code, "Unhandled transform_encoding");
			}
			replace_tclobj(&create_stream_cmd[2], mode);
			TEST_OK_LABEL(finally, code, Tcl_EvalObjv(interp, cmdwords, create_stream_cmd, TCL_EVAL_GLOBAL));
			TEST_OK_LABEL(finally, code, Tcl_ListObjAppendElement(interp, stack, Tcl_GetObjResult(interp)));
			Tcl_ResetResult(interp);
		}

		Tcl_SetHashValue(he, stack);
		Tcl_IncrRefCount(stack);	// For the hash value reference

		replace_tclobj(streamcmd, stack);
	} else {
		replace_tclobj(streamcmd, Tcl_GetHashValue(he));
	}

finally:
	for (int i=0; i<cmdwords; i++)
		replace_tclobj(&create_stream_cmd[i], NULL);
	replace_tclobj(&stack, NULL);
	return code;
}

//>>>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"	// preview_input_line is for gdb use only
static void preview_input_line(const uint8_t* msg, const uint8_t* input) //<<<
{
	const uint8_t*		YYCURSOR = input;
	const uint8_t*		tok;

loop:
	tok = YYCURSOR;
	/*!local:re2c:preview_input_line
		!use:common;

		linechar	= [^] \ [\r\n] \ end;

		linechar+	{ goto loop; }
		crlf		{ fprintf(stderr, "%s: (%.*s)\n", msg, (int)(tok-input), input); return; }
		*			{ fprintf(stderr, "%s: end at %zd: 0x%02x\n", msg, tok-input, (uint8_t)tok[0]); return; }
	*/
}

//>>>
#pragma GCC diagnostic pop
static int64_t now() //<<<
{
	Tcl_Time	t;
	Tcl_GetTime(&t);
	return t.sec*1000000ULL + t.usec;
}

//>>>
static int read_response(Tcl_Interp* interp) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	Tcl_DString			te;
	ssize_t				content_length = -1;
	Tcl_Obj*			decoded = NULL;
	Tcl_Obj*			stream_stack = NULL;

	Tcl_DStringSetLength(&l->context, 0);
	Tcl_DStringAppend(&l->context, "\x7b", 1);
	Tcl_DStringInit(&te);

	replace_tclobj(&l->body, NULL);
	replace_tclobj(&l->starttime, NULL);

	/*!rules:re2c:read_response
		!use:common;

		re2c:define:YYCTYPE		= uint8_t;
		re2c:define:YYCURSOR	= l->cur;
		re2c:define:YYMARKER	= l->mar;
		re2c:define:YYLIMIT		= l->lim;
		re2c:define:YYFILL		= "fill(interp, &code) == 0";
		re2c:tags				= 1;
		re2c:eof				= 0;
		re2c:yyfill:enable		= 1;

		header_content	= vchar ((sp | htab)+ field_vchar)*;
		header_value	= ows @hv1 header_content* ows @hv2 crlf;
		lambda_header
			= 'Lambda-Runtime-Aws-Request-Id' @rid
			| 'Lambda-Runtime-Deadline-Ms'
			| 'Lambda-Runtime-Invoked-Function-Arn'
			| 'Lambda-Runtime-Trace-Id'
			| 'Lambda-Runtime-Client-Context'
			| 'Lambda-Runtime-Cognito-Identity';

		$	{ THROW_ERROR_LABEL(finally, code, "EOF"); }
	*/

	for (;;) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
		const uint8_t	*v1, *v2, *st1, *st2, *rp1, *rp2;	// HTTP version, status, reason_phrase
#pragma GCC diagnostic pop
		/*!stags:re2c:read_statusline	format = "const uint8_t *@@{tag};\n"; */

		l->tok = l->cur;
		/*!local:re2c:read_statusline
			!use:read_response;

			status_line	{ l->http_code = (st1[0]-'0')*100 + (st1[1]-'0')*10 + (st1[2]-'0'); break; }
			*			{ THROW_ERROR_LABEL(finally, code, "Couldn't parse status line"); }
		*/
	}
	replace_tclobj(&l->starttime, Tcl_NewWideIntObj(now()));

header:
	for (;;) {
		const uint8_t	*c1, *c2, *he, *hv1, *hv2, *rid;

		l->tok = l->cur;
		/*!stags:re2c:read_header		format = "const uint8_t *@@{tag}; "; */
		/*!local:re2c:read_header
			!use:read_response;

			lambda_header @he ":" header_value {
				append_json_obj_key(&l->context, l->tok, he - l->tok, hv1, hv2-hv1);
				if (rid) {
					Tcl_DStringSetLength(&l->req_id, 0);
					Tcl_DStringAppend(&l->req_id, (const char*)hv1, hv2-hv1);
					l->responded = 0;
				}
				continue;
			}

			'Content-Length' ":" ows @c1 digit+ @c2 ows crlf {
				if (content_length != -1)
					THROW_ERROR_LABEL(finally, code, "Multiple Content-Length headers");

				const uint8_t*	c = c1;
				size_t			acc = 0;
				while (c < c2) acc = acc*10 + (*c++)-'0';
				content_length = acc;
				continue;
			}
			'Content-Length' ":" header_value	{ THROW_ERROR_LABEL(finally, code, "Invalid Content-Length value"); }

			'Transfer-Encoding:' header_value	{ l->cur = hv1; goto transfer_encoding; }

			field_name    @he ":" header_value {
				//fprintf(stderr, "Ignoring header: (%.*s): (%.*s)\n", he - l->tok, l->tok, hv2-hv1, hv1);
				// TODO: something?
				continue;
			}

			crlf	{ goto body; }
			*		{ THROW_ERROR_LABEL(finally, code, "Failed to parse header"); }
		*/
	}

transfer_encoding:
	if (Tcl_DStringLength(&te)) THROW_ERROR_LABEL(finally, code, "Multiple Transfer-Encoding headers");
	for (;;) {
		l->tok = l->cur;
		/*!local:re2c:transfer_encoding
			!use:read_response;

			encoding
				= "compress"
				| "deflate"
				| "gzip";

			encoding ows "," ows	{ Tcl_DStringAppend(&te, (const char*)l->tok, 1);	continue; }
			"chunked" ows crlf		{ Tcl_DStringAppend(&te, "C", 1);					goto header; }
			*						{ THROW_ERROR_LABEL(finally, code, "Failed to parse encoding"); }
		*/
	}

body:
	switch (l->http_code) { case 100 ... 199: case 204: case 304: goto done; } // No body

	const int telen = Tcl_DStringLength(&te);
	if (telen) {
		Tcl_Obj**	streamv = NULL;
		int			streamc = 0;

		TEST_OK_LABEL(finally, code, get_transform(interp, &te, &stream_stack));
		if (stream_stack) {
			TEST_OK_LABEL(finally, code, Tcl_ListObjGetElements(interp, stream_stack, &streamc, &streamv));
			for (int i=0; i<streamc; i++) { // Reset streams <<<
				TEST_OK_LABEL(finally, code, Tcl_EvalObjv(interp, 2, (Tcl_Obj*[]){
					streamv[i], l->lit[L_RESET]
				}, TCL_EVAL_GLOBAL));
			}
			//>>>
		}

		content_length = -1;	// Transfer-Encoding overrides Content-Length

		// The existence of any saved te implies chunked as the last encoding
		for (;;) {
			const uint8_t	*ce;
			/*!stags:re2c:read_chunked		format = "const uint8_t *@@{tag}; "; */

			l->tok = l->cur;
			/*!local:re2c:read_chunked
				!use:read_response;

				chunksize		= hexdigit+;
				chunk_ext_name	= token;
				chunk_ext_value	= token | quoted_string;
				chunk_ext       = ";" chunk_ext_name ( "=" chunk_ext_value )?;

				"0"+ chunk_ext* crlf	{
					if (0 && streamc) {
						replace_tclobj(&decoded, Tcl_NewObj());
						for (int i=0; i<streamc; i++) { // Flush streams
							TEST_OK_LABEL(finally, code, Tcl_EvalObjv(interp, 4, (Tcl_Obj*[]){
								streamv[i], l->lit[L_ADD], l->lit[L_FINALIZE], decoded
							}, TCL_EVAL_GLOBAL));
							replace_tclobj(&decoded, Tcl_GetObjResult(interp));
						}
						Tcl_ResetResult(interp);

						if (l->body) Tcl_AppendObjToObj(l->body, decoded);
						else         replace_tclobj(&l->body, decoded);
					}
					break;
				}

				chunksize @ce chunk_ext* crlf	{
					ssize_t			chunklen = 0;
					const uint8_t*	p = l->tok;
					while (p < ce) {
						const int	ch = *p++;
						if      (ch >= 'a') chunklen = (chunklen<<4) + ch - 'a' + 10;
						else if (ch >= 'A') chunklen = (chunklen<<4) + ch - 'A' + 10;
						else                chunklen = (chunklen<<4) + ch - '0';
						if (chunklen < 0) THROW_ERROR_LABEL(finally, code, "Chunk length too long");
					}

					size_t	remain = chunklen;

					while (remain) {
						l->tok = l->cur;

						// Attempt to read more from the socket if we need it
						if (l->tok >= l->lim && fill(interp, &code) != 0) goto finally;

						size_t	avail = l->lim - l->tok;
						size_t	got = avail < remain ? avail : remain;

						replace_tclobj(&decoded, Tcl_NewByteArrayObj(l->tok, got));

						// Send the bytes through the transform stream(s)
						for (int i=0; i<streamc; i++) {
							TEST_OK_LABEL(finally, code, Tcl_EvalObjv(interp, 3, (Tcl_Obj*[]){
								streamv[i], l->lit[L_ADD], decoded
							}, TCL_EVAL_GLOBAL));
							replace_tclobj(&decoded, Tcl_GetObjResult(interp));
						}

						if (l->body) Tcl_AppendObjToObj(l->body, decoded);
						else         replace_tclobj(&l->body, decoded);

						remain -= got;
						l->cur += got;
					}

					if (l->cur[0] == '\r' || l->cur[1] == '\n') l->cur += 2;
					else if (l->cur[0] == '\n')                 l->cur++;
					else THROW_ERROR_LABEL(finally, code, "Missing CRLF after chunk");

					continue;
				}

				*	{ THROW_ERROR_LABEL(finally, code, "Failed to parse chunk" ); }
			*/
		}

		// trailer
		for (;;) {
			const uint8_t	*he, *hv1, *hv2, *rid;
			/*!stags:re2c:read_trailer		format = "const uint8_t *@@{tag}; "; */

			l->tok = l->cur;
			/*!local:re2c:read_trailer
				!use:read_response;

				// Ignore trailer forbidden headers
				trailer_forbidden
					= 'Content-Type'
					| 'Transfer-Encoding'
					| 'Host'
					| 'If-' field_name
					| 'WWW-Authenticate'
					| 'Authorization'
					| 'Proxy-Authenticate'
					| 'Proxy-Authorization'
					| 'Cookie'
					| 'Set-Cookie'
					| 'Age'
					| 'Cache-Control'
					| 'Expires'
					| 'Date'
					| 'Location'
					| 'Retry-After'
					| 'Vary'
					| 'Warning'
					| 'Content-Encoding'
					| 'Content-Type'
					| 'Content-Range'
					| 'Trailer';

				trailer_forbidden ":" ows field_value ows crlf	{ continue; }

				lambda_header @he ":" header_value {
					append_json_obj_key(&l->context, l->tok, he - l->tok, hv1, hv2-hv1);
					if (rid) {
						Tcl_DStringSetLength(&l->req_id, 0);
						Tcl_DStringAppend(&l->req_id, (const char*)hv1, hv2-hv1);
					}
					continue;
				}

				field_name    @he ":" header_value {
					fprintf(stderr, "Ignoring trailer: (%.*s): (%.*s)\n", (int)(he - l->tok), l->tok, (int)(hv2-hv1), hv1);
					// TODO: something?
					continue;
				}

				crlf	{ break; }
				*		{ THROW_ERROR_LABEL(finally, code, "Failed to parse trailer"); }
			*/
		}
	} else if (content_length >= 0) {
		size_t	remain = content_length;

		while (remain) {
			l->tok = l->cur;

			// Attempt to read more from the socket if we need it
			if (l->tok >= l->lim && fill(interp, &code) != 0) goto finally;

			const size_t	avail = l->lim - l->tok;
			const size_t	got = avail < remain ? avail : remain;

			replace_tclobj(&decoded, Tcl_NewByteArrayObj(l->tok, got));

			if (l->body) Tcl_AppendObjToObj(l->body, decoded);
			else         replace_tclobj(&l->body, decoded);

			remain -= got;
			l->cur += got;
		}
	} else {
		// To EOF not supported
		THROW_ERROR_LABEL(finally, code, "Can't determine body framing");
	}

done:
	if (!l->body) replace_tclobj(&l->body, l->lit[L_EMPTY]);
	Tcl_DStringAppend(&l->context, "\x7d", 1);
	l->pipeline_depth--;

finally:
	Tcl_DStringFree(&te);
	replace_tclobj(&stream_stack, NULL);
	replace_tclobj(&decoded, NULL);
	return code;
}

//>>>
static int handler_bottom(ClientData cdata[], Tcl_Interp* interp, int code);
static int handler_top(Tcl_Interp* interp) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;

	replace_tclobj(&l->starttime, NULL);
	TEST_OK_LABEL(finally, code, send_req(interp, "GET", 3, &l->ds_next, NULL, 0));
	// Read off async responses
	while (l->pipeline_depth > 1) {
		TEST_OK_LABEL(finally, code, read_response(interp));
		if (l->http_code >= 200 || l->http_code <= 299) continue;
		fprintf(stderr, "HTTP status %03d to pipelined req\n", l->http_code);
	}

	TEST_OK_LABEL(finally, code, read_response(interp));

	Tcl_Obj*	cmd = NULL;

	replace_tclobj(&cmd, Tcl_NewListObj(4, (Tcl_Obj*[]){
		l->lit[L_APPLY], l->handler, l->body, Tcl_DStringToObj(&l->context)
	}));

	Tcl_NRAddCallback(interp, handler_bottom, cmd, NULL, NULL, NULL);
	code = Tcl_NREvalObj(interp, cmd, 0);

finally:
	return code;
}

//>>>
static int handler_bottom(ClientData cdata[], Tcl_Interp* interp, int code) //<<<
{
	struct interp_cx*	l = (struct interp_cx*)cdata;
	Tcl_Obj*			cmd	= cdata[0];

	replace_tclobj(&cmd,   NULL);

	if (code != TCL_OK) {
		fprintf(stderr, "Error from handler: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));
		if (!l->responded)
			TEST_OK_LABEL(finally, code, Lambdaric_SendResponse(interp, "error",
				"{\"errorMessage\":\"Function invoke error\",\"errorType\":\"InternalError\"}", -1));
	} else {
		if (!l->responded)
			TEST_OK_LABEL(finally, code, Lambdaric_SendResponse(interp, "error",
				"{\"errorMessage\":\"No response from function\",\"errorType\":\"NoResponse\"}", -1));
	}

	code = handler_top(interp);

finally:
	return code;
}

//>>>
static int setup_cb(ClientData cdata[], Tcl_Interp* interp, int code) //<<<
{
	Tcl_DString	ds;
	Tcl_DStringInit(&ds);

	if (code != TCL_OK) {
		Tcl_DStringAppend(&ds, "runtime/init/error", -1);
		TEST_OK_LABEL(finally, code, send_req(interp, "POST", 4, &ds,
			"{\"errorMessage\":\"Failed to load function.\",\"errorType\":\"InvalidFunctionException\"}", -1));
		goto finally;
	}

	code = handler_top(interp);

finally:
	Tcl_DStringFree(&ds);
	return code;
}

//>>>
//>>>
// Stubs API <<<
int Lambdaric_SendResponsev(Tcl_Interp* interp, const char* tail, struct iovec* body, int iovcnt) //<<<
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, PACKAGE_NAME, NULL);
	int					code = TCL_OK;
	Tcl_DString			ds;

	Tcl_DStringInit(&ds);

	if (Tcl_DStringLength(&l->req_id) == 0) THROW_ERROR_LABEL(finally, code, "No request context");
	if (l->responded) THROW_ERROR_LABEL(finally, code, "Already responded to this request");

	Tcl_DStringAppend(&ds, "runtime/invocation/", -1);
	path_encode(&ds, Tcl_DStringValue(&l->req_id));
	Tcl_DStringAppend(&ds, "/", 1);
	Tcl_DStringAppend(&ds, tail, -1);
	TEST_OK_LABEL(finally, code, send_reqv(interp, "POST", 4, &ds, body, iovcnt));
	l->responded = 1;

finally:
	Tcl_DStringFree(&ds);
	return code;
}

//>>>
int Lambdaric_SendResponse(Tcl_Interp* interp, const char* tail, const void* body, int bodylen) //<<<
{
	const int	len = bodylen==-1 ? strlen(body) : bodylen;

	if (len) return Lambdaric_SendResponsev(interp, tail, (struct iovec[]){ {(void*)body, len} }, 1);
	else     return Lambdaric_SendResponsev(interp, tail, NULL,                                   0);
}

//>>>
//>>>
// Script API <<<
OBJCMD(lambdaric) //<<<
{
	struct interp_cx*	l = cdata;
	static const char*	ops[] = {
		"start",
		"respond",
		"starttime",
		NULL
	};
	enum {
		OP_START,
		OP_RESPOND,
		OP_STARTTIME
	};
	int			code = TCL_OK;
	int			op;

	enum {A_cmd, A_OP, A_args};
	CHECK_MIN_ARGS_LABEL(finally, code, "op ?arg ...?");

	TEST_OK_LABEL(finally, code, Tcl_GetIndexFromObj(interp, objv[A_OP], ops, "op", TCL_EXACT, &op));

	switch (op) {
		case OP_START: //<<<
		{
			enum {A_cmd=A_OP, A_SCRIPT, A_HANDLER, A_objc};
			CHECK_ARGS_LABEL(finally, code, "script handler");
			replace_tclobj(&l->handler, objv[A_HANDLER]);
			Tcl_NRAddCallback(interp, setup_cb, NULL, NULL, NULL, NULL);
			code = Tcl_NREvalObj(interp, objv[A_SCRIPT], 0);
			goto finally;
		}
		//>>>
		case OP_RESPOND: //<<<
		{
			enum {A_cmd=A_OP, A_RESPONSE, A_objc};
			CHECK_ARGS_LABEL(finally, code, "response");

			int			response_len;
			const char*	response = Tcl_GetStringFromObj(objv[A_RESPONSE], &response_len);

			TEST_OK_LABEL(finally, code, Lambdaric_SendResponse(interp, "response", response, response_len));
			break;
		}
		//>>>
		case OP_STARTTIME: //<<<
		{
			enum {A_cmd=A_OP, A_objc};
			CHECK_ARGS_LABEL(finally, code, "");
			if (!l->starttime) THROW_ERROR_LABEL(finally, code, "Not in a request context");
			Tcl_SetObjResult(interp, l->starttime);
			break;
		}
		//>>>
		default: THROW_ERROR_LABEL(finally, code, "Unhandled op \"", Tcl_GetString(objv[A_OP]), "\"");
	}

finally:
	return code;
}

//>>>
#if TESTMODE
OBJCMD(testmode) //<<<
{
	struct interp_cx*	l = (struct interp_cx*)cdata;
	int					code = TCL_OK;

	enum {A_cmd, A_objc};
	CHECK_ARGS_LABEL(finally, code);

	Tcl_SetObjResult(interp, l->lit[L_TRUE]);

finally:
	return code;
}

//>>>
#endif

static struct cmd {
	char*			name;
	Tcl_ObjCmdProc*	proc;
	Tcl_ObjCmdProc*	nrproc;
} cmds[] = {
	{   "::lambdaric",						lambdaric,			NULL},
	/*
	{NS "::apig_event",						apig_event,			NULL},
	*/
#if TESTMODE
	{NS "::_testmode_testmode",				testmode,			NULL},
#endif
	{0}
};
// Script API >>>

extern const LambdaricStubs* const lambdaricConstStubsPtr;

#ifdef __cplusplus
extern "C" {
#endif
DLLEXPORT int Lambdaric_Init(Tcl_Interp* interp) //<<<
{
	int					code = TCL_OK;
	struct interp_cx*	l = NULL;

#if USE_TCL_STUBS
	if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) return TCL_ERROR;
	if (Tcl_OOInitStubs(interp) == NULL)               return TCL_ERROR;
#endif

	TEST_OK_LABEL(finally, code, obstack_pool_init(interp));

	Tcl_MutexLock(&g_intreps_mutex);
	if (!g_intreps_refcount++) Tcl_InitHashTable(&g_intreps, TCL_ONE_WORD_KEYS);
	Tcl_MutexUnlock(&g_intreps_mutex);

	l =  (struct interp_cx*)ckalloc(sizeof *l);
	*l = (struct interp_cx){
		.sock		= -1
	};

	for (int i=0; i<L_size; i++)
		replace_tclobj(&l->lit[i], Tcl_NewStringObj(lit_str[i], -1));

	obstack_init(&l->ob);

	l->rx_buf[RX_BUF_SIZE] = 0;

	Tcl_DStringInit(&l->url);
	Tcl_DStringAppend(&l->url, "/2018-06-01/", -1);
	l->url_base_len = Tcl_DStringLength(&l->url);
	const char*	api = getenv("AWS_LAMBDA_RUNTIME_API");
	if (api == NULL) THROW_ERROR_LABEL(finally, code, "Can't get AWS_LAMBDA_RUNTIME_API environment variable");
	//fprintf(stderr, "Parsing %s\n", api);
	TEST_OK_LABEL(finally, code, parse_runtime_api(interp, (const uint8_t*)api));
	//fprintf(stderr, "Resolving %s, host: (%s)\n", api, l->api_host);
	TEST_OK_LABEL(finally, code, connect_runtime_api(interp, l->api_host, l->api_port, &l->sock));
	//fprintf(stderr, "Connected: %d\n", l->sock);
	Tcl_DStringInit(&l->ds_next);
	Tcl_DStringInit(&l->req_id);
	Tcl_DStringAppend(&l->ds_next, "runtime/invocation/next", -1);
	Tcl_InitHashTable(&l->transforms, TCL_STRING_KEYS);

	Tcl_DStringInit(&l->context);

	Tcl_Namespace*		ns = Tcl_CreateNamespace(interp, NS, NULL, NULL);
	TEST_OK_LABEL(finally, code, Tcl_Export(interp, ns, "*", 0));

	struct cmd*	c = cmds;
	while (c->name) {
		Tcl_Command		r = NULL;
		if (c->nrproc) {
			r = Tcl_NRCreateCommand(interp, c->name, c->proc, c->nrproc, l, NULL);
		} else {
			r = Tcl_CreateObjCommand(interp, c->name, c->proc, l, NULL);
		}
		if (r == NULL) {
			code = TCL_ERROR;
			goto finally;
		}
		c++;
	}

	{ // Publish build config with Tcl_RegisterConfig <<<
		Tcl_Obj*		buildheader = NULL;
		Tcl_Obj*		generic = NULL;
		Tcl_Obj*		h = NULL;
		Tcl_Obj*		dirvar = NULL;
		Tcl_Obj*		dir = NULL;
		Tcl_StatBuf*	stat = NULL;

		Tcl_MutexLock(&g_config_mutex); //<<<

		// Only do this the first time, later loads might be doing "load {} ..."
		if (g_config_refcount++ == 0) {
			replace_tclobj(&dirvar, Tcl_NewStringObj("dir", 3));
			dir = Tcl_ObjGetVar2(interp, dirvar, NULL, TCL_LEAVE_ERR_MSG);
			if (!dir) {
				code = TCL_ERROR;
				goto config_finally;
			}

			replace_tclobj(&g_packagedir, Tcl_FSGetNormalizedPath(interp, dir));

			replace_tclobj(&generic,	Tcl_NewStringObj("generic", -1));
			replace_tclobj(&h,			Tcl_NewStringObj("lambdaric.h", -1));
			replace_tclobj(&buildheader, Tcl_FSJoinToPath(g_packagedir, 2, (Tcl_Obj*[]){generic, h}));

			stat = Tcl_AllocStatBuf();
			if (0 == Tcl_FSStat(buildheader, stat)) {
				// Running from build dir
				replace_tclobj(&g_includedir, Tcl_FSJoinToPath(g_packagedir, 1, (Tcl_Obj*[]){generic}));
			} else {
				replace_tclobj(&g_includedir, g_packagedir);
			}

			cfg[0].value = Tcl_GetString(g_packagedir);		// Under global ref
			cfg[1].value = Tcl_GetString(g_includedir);		// Under global ref
			cfg[2].value = Tcl_GetString(g_packagedir);		// Under global ref

			Tcl_RegisterConfig(interp, PACKAGE_NAME, cfg, "utf-8");
		}

	config_finally:
		Tcl_MutexUnlock(&g_config_mutex); //>>>

		replace_tclobj(&buildheader, NULL);
		replace_tclobj(&generic, NULL);
		replace_tclobj(&h, NULL);
		replace_tclobj(&dirvar, NULL);

		if (stat) {
			ckfree(stat);
			stat = NULL;
		}
	}
	//>>>

	TEST_OK_LABEL(finally, code, Tcl_PkgProvideEx(interp, PACKAGE_NAME, PACKAGE_VERSION, lambdaricConstStubsPtr));

	Tcl_SetAssocData(interp, PACKAGE_NAME, free_interp_cx, l);

finally:
	if (code != TCL_OK) {
		if (l) {
			free_interp_cx(l, interp);
			l = NULL;
		}
	}
	return code;
}

//>>>
#if UNLOAD
DLLEXPORT int Lambdaric_Unload(Tcl_Interp* interp, int flags) //<<<
{
	int			code = TCL_OK;
	Tcl_HashSearch	search;

	Tcl_MutexLock(&g_intreps_mutex); //<<<
	if (--g_intreps_refcount <= 0) {
		// Downgrade all our objtype instances to pure strings.  Have to
		// restart the hash walk each time because updating the string rep
		// and freeing the internal can change other entries.
		Tcl_HashEntry*	he = NULL;
		while ((he = Tcl_FirstHashEntry(&g_intreps, &search))) {
			Tcl_Obj*	obj = Tcl_GetHashValue(he);
			if (obj) {
				Tcl_GetString(obj);
				Tcl_FreeInternalRep(obj);
			}
		}
		Tcl_DeleteHashTable(&g_intreps);
	}
	Tcl_MutexUnlock(&g_intreps_mutex); //>>>

	Tcl_DeleteAssocData(interp, PACKAGE_NAME);	// Have to do this here, otherwise Tcl will try to call it after we're unloaded

	if (g_intreps_refcount <= 0) {
		Tcl_MutexFinalize(&g_intreps_mutex);
		g_intreps_mutex = NULL;
	}

	Tcl_MutexLock(&g_config_mutex); //<<<
	g_config_refcount--;
	if (g_config_refcount <= 0) {
		replace_tclobj(&g_packagedir, NULL);
		replace_tclobj(&g_includedir, NULL);
	}
	Tcl_MutexUnlock(&g_config_mutex); //>>>
	if (g_config_refcount <= 0) {
		Tcl_MutexFinalize(&g_config_mutex);
		g_config_mutex = NULL;
	}

	TEST_OK_LABEL(finally, code, obstack_pool_fini(interp));

finally:
	return code;
}

//>>>
#endif //UNLOAD
#ifdef __cplusplus
}
#endif

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
