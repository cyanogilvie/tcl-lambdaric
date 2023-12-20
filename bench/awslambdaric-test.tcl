package require rl_http 1.14.12
package require rl_json
package require parse_args
interp alias {} json {} ::rl_json::json
interp alias {} parse_args {} ::parse_args::parse_args

oo::define rl_http method _response_start line {set ::response_start [clock microseconds]}

set running			true
set hooks			{}
set hooks_oneshot	{}

# Read env vars
if {[info exists env(HANDLER)]} {
	set handler	$env(HANDLER)	;# Explicitly configured environment variable - container functions don't have _HANDLER set to anything
} elseif {[info exists env(_HANDLER)]} {
	set handler	$env(_HANDLER)	;# The location to the handler, from the function's configuration. The standard format is file.method, where file is the name of the file without an extension, and method is the name of a method or function that's defined in the file.
} else {
	set handler	[lindex $argv 0]
}
set function	[lindex [split $handler .] 1]
set api		$env(AWS_LAMBDA_RUNTIME_API)	;# The host and port of the runtime API.

try $::restore_env

set api_version	2018-06-01
set api_url		http://$api/$api_version

proc create_handler_interp {} { #<<<
	global function

	interp create handler
	handler eval {
		package require rl_json
		interp alias {} json {} ::rl_json::json

		namespace eval ::lambda {
			namespace export *
			namespace ensemble create -prefixes no
		}
	}
	handler eval [list proc $function [info args $function] [info body $function]]
	handler alias ::lambda::reload_handler	reload_handler
	handler alias ::lambda::hook			hook
	handler alias ::lambda::shutdown		shutdown
}

#>>>
proc hook args { #<<<
	global hooks hooks_oneshot

	parse_args $args {
		-oneshot	{-boolean -# {Delete the hook after running it once}}
		hook		{-required -enum {pre_handle post_handle unload shutdown}}
		cb			{-required}
	}

	if {$oneshot} {
		dict lappend hooks_oneshot $hook $cb
	} else {
		dict lappend hooks $hook $cb
	}
	return
}

#>>>
proc run_hooks args { #<<<
	global hooks hooks_oneshot
	parse_args $args {
		hook			{-required -enum {pre_handle post_handle unload shutdown}}
		args			{-name hookargs}
	}

	set callbacks	{}

	if {[dict exists $hooks_oneshot $hook]} {
		lappend callbacks {*}[dict get $hooks_oneshot $hook]
		dict unset hooks_oneshot $hook
	}

	if {[dict exists $hooks $hook]} {
		lappend callbacks {*}[dict get $hooks $hook]
	}

	foreach callback $callbacks {
		try {
			handler eval [list {*}$callback {*}$hookargs]
		} on continue {} {
		} on break {} {
			break
		} on error {errmsg options} {
			set errorcode	[dict get $options -errorcode]
			puts [json template {
				{"lvl": "error", "msg": "~S:errmsg", "errorcode": "~S:errorcode"}
			}]
		}
	}
}

#>>>
proc reload_handler {} { #<<<
	if {[interp exists handler]} {
		run_hooks unload
		interp delete handler
	}
	create_handler_interp
	return
}

#>>>
proc shutdown {} { #<<<
	global running
	puts {{"lvl":"notice","msg":"Shutdown signalled"}}
	set running false
}

#>>>

try {
	# Initialize function
	reload_handler
} on error {errmsg options} {
	# Handle errors
	#  If an error occurs, call the initialization error API (https://docs.aws.amazon.com/lambda/latest/dg/runtimes-api.html#runtimes-api-initerror) and exit immediately
	puts stderr "Failed to load function: $errmsg\n[dict get $options -errorinfo]"
	rl_http instvar h POST $api_url/runtime/init/error -data [json template {
		{
			"errorMessage": "Failed to load function.",
			"errorType":	"InvalidFunctionException"
		}
	}]
	exit 1
}
puts "Loaded handler"


# Process incoming events in a loop
while {$running} {
	try {
		# Get an event <<<
		# Call the next invocation API (https://docs.aws.amazon.com/lambda/latest/dg/runtimes-api.html#runtimes-api-next) to get the next event. The response body contains the event data. Response headers contain the request ID and other information.
		unset -nocomplain ::response_start
		if {[info exists wakeup_time]} {
			set elapsed_ms	[expr {([clock microseconds] - $wakeup_time)/1e3}]
			puts [format {%5.3f invocation/next} $elapsed_ms]
		}
		rl_http instvar h GET $api_url/runtime/invocation/next -timeout 31536000

		switch -- [$h code] {
			200 {}
			403 - 500 {
				puts stderr "Got [$h code] response to /runtime/invocation/next: [$h body], exiting"
				exit 1
			}

			default {
				puts stderr "Got unexpected HTTP status [$h code] to /runtime/invocation/next, exiting"
				exit 1
			}
		}
		if {[info exists ::response_start]} {
			set wakeup_time	$::response_start
			set elapsed_ms	[expr {([clock microseconds] - $wakeup_time)/1e3}]
			puts [format {%5.3f Read lambda event} $elapsed_ms]
		}

		set event	[$h body]

		foreach {header var} {
			lambda-runtime-aws-request-id		req_id
			lambda-runtime-deadline-ms			deadline
			lambda-runtime-invoked-function-arn	function_arn
			lambda-runtime-trace-id				trace_id
			lambda-runtime-client-context		client_context
			lambda-runtime-cognito-identity		cogito_identity
		} {
			if {[dict exists [$h headers] $header]} {
				set $var	[lindex [dict get [$h headers] $header] 0]
			} else {
				unset -nocomplain $var
			}
		}
		#>>>

		# Propagate the tracing header <<<
		# Get the X-Ray tracing header from the Lambda-Runtime-Trace-Id header in the API response. Set the _X_AMZN_TRACE_ID environment variable with the same value for the X-Ray SDK to use.
		if {[info exists trace_id]} {
			set env(_X_AMZN_TRACE_ID)	$trace_id
		}
		#>>>

		# Create a context object
		# Create an object with context information from environment variables and headers in the API response.
		set context		[json template {
			{
				"Lambda-Runtime-Aws-Request-Id":		"~S:req_id",
				"Lambda-Runtime-Deadline-Ms":			"~S:deadline",
				"Lambda-Runtime-Invoked-Function-Arn":	"~S:function_arn",
				"Lambda-Runtime-Trace-Id":				"~S:trace_id",
				"Lambda-Runtime-Client-Context":		"~S:client_context",
				"Lambda-Runtime-Cognito-Identity":		"~S:cogito_identity"
			}
		}]

		run_hooks pre_handle
		if {[info exists env(STAGE)]} {
			json set context stage [json string $env(STAGE)]
		}
		# Invoke the function handler
		# Pass the event and context object to the handler.
		if {[info exists wakeup_time]} {
			set elapsed_ms	[expr {([clock microseconds] - $wakeup_time)/1e3}]
			puts [format {%5.3f Sending event to handler} $elapsed_ms]
		}
		if {[info exists ::response_start]} {handler eval [list set ::_lambda_start $::response_start]}
		set response	[handler eval [list $function $event $context]]

		# Handle the response
		# Call the invocation response API (https://docs.aws.amazon.com/lambda/latest/dg/runtimes-api.html#runtimes-api-response) to post the response from the handler.
		if {[info exists wakeup_time]} {
			set elapsed_ms	[expr {([clock microseconds] - $wakeup_time)/1e3}]
			puts [format {%5.3f Sending response to lambda} $elapsed_ms]
		}
		rl_http instvar h POST $api_url/runtime/invocation/$req_id/response -data $response
		switch -- [$h code] {
			202 { # Accepted
			}
			400 { # Bad request
				puts stderr "Got 400 response to invocation response POST: [$h body]"
			}
			403 { # Forbidden
				puts stderr "Got 403 response to invocation response POST: [$h body]"
			}
			413 { # Payload too large
				puts stderr "Got 413 response to invocation response POST: [$h body]"
			}
			500 { # Container error
				puts stderr "Got 500 response to invocation response POST: [$h body]"
				exit 1
			}
			default {
				puts stderr "Got unexpected HTTP status [$h code] to invocation response"
				exit 1
			}
		}
		if {[info exists wakeup_time]} {
			set elapsed_ms	[expr {([clock microseconds] - $wakeup_time)/1e3}]
			puts [format {%5.3f Response sent} $elapsed_ms]
		}
	} on error {errmsg options} {
		# Handle errors
		# If an error occurs, call the invocation error API. (https://docs.aws.amazon.com/lambda/latest/dg/runtimes-api.html#runtimes-api-invokeerror)
		set msg			"Unhandled error processing invocation: $errmsg"
		set errorcode	[dict get $options -errorcode]
		set errorinfo	[dict get $options -errorinfo]
		puts stderr [json template {
			{
				"lvl":			"error",
				"msg":			"~S:msg",
				"errorcode":	"~S:errorcode",
				"errorinfo":	"~S:errorinfo"
			}
		}]
		rl_http instvar h POST $api_url/runtime/invocation/$req_id/error -data [json template {
			{
				"errorMessage": "Internal error processing the invocation",
				"errorType":	"InvalidEventDataException"
			}
		}]
	} finally {
		# Cleanup
		# Release unused resources, send data to other services, or perform additional tasks before getting the next event.
		run_hooks post_handle
	}
}

run_hooks shutdown

# vim: ft=tcl foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4

