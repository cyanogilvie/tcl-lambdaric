#!/bin/bash -norc
dnl	This file is an input file used by the GNU "autoconf" program to
dnl	generate the file "configure", which is run during Tcl installation
dnl	to configure the system for the local environment.

#-----------------------------------------------------------------------
# Sample configure.ac for Tcl Extensions.  The only places you should
# need to modify this file are marked by the string __CHANGE__
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
# __CHANGE__
# Set your package name and version numbers here.
#
# This initializes the environment with PACKAGE_NAME and PACKAGE_VERSION
# set as provided.  These will also be added as -D defs in your Makefile
# so you can encode the package version directly into the source files.
# This will also define a special symbol for Windows (BUILD_<PACKAGE_NAME>
# so that we create the export library with the dll.
#-----------------------------------------------------------------------

AC_INIT([lambdaric], [0.1])

#--------------------------------------------------------------------
# Call TEA_INIT as the first TEA_ macro to set up initial vars.
# This will define a ${TEA_PLATFORM} variable == "unix" or "windows"
# as well as PKG_LIB_FILE and PKG_STUB_LIB_FILE.
#--------------------------------------------------------------------

touch generic/lambdaric.h

TEA_INIT()

AC_CONFIG_AUX_DIR(tclconfig)

#--------------------------------------------------------------------
# Load the tclConfig.sh file
#--------------------------------------------------------------------

TEA_PATH_TCLCONFIG
TEA_LOAD_TCLCONFIG

#--------------------------------------------------------------------
# Load the tkConfig.sh file if necessary (Tk extension)
#--------------------------------------------------------------------

#TEA_PATH_TKCONFIG
#TEA_LOAD_TKCONFIG

#-----------------------------------------------------------------------
# Handle the --prefix=... option by defaulting to what Tcl gave.
# Must be called after TEA_LOAD_TCLCONFIG and before TEA_SETUP_COMPILER.
#-----------------------------------------------------------------------

TEA_PREFIX

#-----------------------------------------------------------------------
# Standard compiler checks.
# This sets up CC by using the CC env var, or looks for gcc otherwise.
# This also calls AC_PROG_CC and a few others to create the basic setup
# necessary to compile executables.
#-----------------------------------------------------------------------

TEA_SETUP_COMPILER

# Check for feature toggles
ENABLE_DEBUG
ENABLE_DEFAULT_UNLOAD
ENABLE_TESTMODE

#-----------------------------------------------------------------------
# __CHANGE__
# Specify the C source files to compile in TEA_ADD_SOURCES,
# public headers that need to be installed in TEA_ADD_HEADERS,
# stub library C source files to compile in TEA_ADD_STUB_SOURCES,
# and runtime Tcl library files in TEA_ADD_TCL_SOURCES.
# This defines PKG(_STUB)_SOURCES, PKG(_STUB)_OBJECTS, PKG_HEADERS
# and PKG_TCL_SOURCES.
#-----------------------------------------------------------------------

TEABASE_ADD_SOURCES([lexers/lambdaric.c lambdaricStubInit.c generic/obstackpool.c])
TEA_ADD_HEADERS([generic/lambdaric.h generic/lambdaricDecls.h])
TEA_ADD_INCLUDES([-Igeneric])
TEA_ADD_LIBS([])
TEA_ADD_CFLAGS([])
TEA_ADD_STUB_SOURCES([lambdaricStubLib.c])
TEA_ADD_TCL_SOURCES([])

#--------------------------------------------------------------------
# __CHANGE__
#
# You can add more files to clean if your extension creates any extra
# files by extending CLEANFILES.
# Add pkgIndex.tcl if it is generated in the Makefile instead of ./configure
# and change Makefile.in to move it from CONFIG_CLEAN_FILES to BINARIES var.
#
# A few miscellaneous platform-specific items:
# TEA_ADD_* any platform specific compiler/build info here.
#--------------------------------------------------------------------

CLEANFILES="$CLEANFILES pkgIndex.tcl lexers/*"
if test "${TEA_PLATFORM}" = "windows" ; then
    # Ensure no empty if clauses
    :
    #TEA_ADD_SOURCES([win/winFile.c])
    #TEA_ADD_INCLUDES([-I\"$(${CYGPATH} ${srcdir}/win)\"])
else
    # Ensure no empty else clauses
    :
    #TEA_ADD_SOURCES([unix/unixFile.c])
    #TEA_ADD_LIBS([-lsuperfly])
fi

#--------------------------------------------------------------------
# __CHANGE__
# Choose which headers you need.  Extension authors should try very
# hard to only rely on the Tcl public header files.  Internal headers
# contain private data structures and are subject to change without
# notice.
# This MUST be called after TEA_LOAD_TCLCONFIG / TEA_LOAD_TKCONFIG
#--------------------------------------------------------------------

TEA_PUBLIC_TCL_HEADERS
#TEA_PRIVATE_TCL_HEADERS

#TEA_PUBLIC_TK_HEADERS
#TEA_PRIVATE_TK_HEADERS
#TEA_PATH_X

#--------------------------------------------------------------------
# Check whether --enable-threads or --disable-threads was given.
# This auto-enables if Tcl was compiled threaded.
#--------------------------------------------------------------------

TEA_ENABLE_THREADS

#--------------------------------------------------------------------
# The statement below defines a collection of symbols related to
# building as a shared library instead of a static library.
#--------------------------------------------------------------------

TEA_ENABLE_SHARED

#--------------------------------------------------------------------
# This macro figures out what flags to use with the compiler/linker
# when building shared/static debug/optimized objects.  This information
# can be taken from the tclConfig.sh file, but this figures it all out.
#--------------------------------------------------------------------

TEA_CONFIG_CFLAGS

#--------------------------------------------------------------------
# Set the default compiler switches based on the --enable-symbols option.
#--------------------------------------------------------------------

TEA_ENABLE_SYMBOLS

# Check for required polyfill
TIP445
TEABASE_INIT()

# Build tools needed on the build host to produce some of the source
# files.  This macro attempts to set CC_FOR_BUILD to a compiler that
# can produce binaries that run on the build host rather than the
# target / host when cross-compiling
AX_CC_FOR_BUILD

# Test for -fprofile-partial-training, introduced in GCC 10
AX_CHECK_COMPILE_FLAG([-fprofile-partial-training],
					  [AC_SUBST(PGO_BUILD,"-fprofile-use=prof -fprofile-partial-training")],
					  [AC_SUBST(PGO_BUILD,"-fprofile-use=prof")],
					  [-Werror])

#--------------------------------------------------------------------
# Set the default compiler switches based on the --enable-symbols option.
#--------------------------------------------------------------------

TEA_ENABLE_SYMBOLS

#--------------------------------------------------------------------
# This macro generates a line to use when building a library.  It
# depends on values set by the TEA_ENABLE_SHARED, TEA_ENABLE_SYMBOLS,
# and TEA_LOAD_TCLCONFIG macros above.
#--------------------------------------------------------------------

TEA_MAKE_LIB

#--------------------------------------------------------------------
# Determine the name of the tclsh and/or wish executables in the
# Tcl and Tk build directories or the location they were installed
# into. These paths are used to support running test cases only,
# the Makefile should not be making use of these paths to generate
# a pkgIndex.tcl file or anything else at extension build time.
#--------------------------------------------------------------------

TEA_PROG_TCLSH
#TEA_PROG_WISH

#--------------------------------------------------------------------
# Setup a *Config.sh.in configuration file.
#--------------------------------------------------------------------

TEA_EXPORT_CONFIG([lambdaric])
#AC_SUBST(SAMPLE_VAR)

#--------------------------------------------------------------------
# Specify files to substitute AC variables in. You may alternatively
# have a special pkgIndex.tcl.in or other files which require
# substituting the AC variables in. Include these here.
#--------------------------------------------------------------------

TEAX_CONFIG_INCLUDE_LINE(LAMBDARIC_INCLUDE_SPEC,		[${includedir}])
TEAX_CONFIG_LINK_LINE(LAMBDARIC_LIB_SPEC,
					  [${libdir}/${PACKAGE_NAME}${PACKAGE_VERSION}], [${PACKAGE_NAME}])
TEAX_CONFIG_LINK_LINE(LAMBDARIC_STUB_LIB_SPEC,
					  [${libdir}/${PACKAGE_NAME}${PACKAGE_VERSION}], [${PACKAGE_NAME}stub])

BUILD_DIR="$(pwd | sed -e 's/ /\\\\ /g')"
TEAX_CONFIG_INCLUDE_LINE(LAMBDARIC_BUILD_INCLUDE_SPEC, [${BUILD_DIR}/generic])
TEAX_CONFIG_LINK_LINE(LAMBDARIC_BUILD_LIB_SPEC,
					 [${BUILD_DIR}], [${PACKAGE_NAME}])
TEAX_CONFIG_LINK_LINE(LAMBDARIC_BUILD_STUB_LIB_SPEC,
					 [${BUILD_DIR}], [${PACKAGE_NAME}stub])

#eval "LAMBDARIC_INCLUDE_PATH_INSTALL=\"${includedir}\""
eval "LAMBDARIC_INCLUDE_PATH_INSTALL=\"${libdir}/${PACKAGE_NAME}${PACKAGE_VERSION}\""
eval "LAMBDARIC_LIBRARY_PATH_INSTALL=\"${libdir}/${PACKAGE_NAME}${PACKAGE_VERSION}\""
eval "LAMBDARIC_PACKAGE_PATH_INSTALL=\"${libdir}/${PACKAGE_NAME}${PACKAGE_VERSION}\""
AS_IF([test ${TCL_LIB_VERSIONS_OK} = nodots], [
	eval "LAMBDARIC_LIBRARY=\"lib${PACKAGE_NAME}${TCL_TRIM_DOTS}\""
	eval "LAMBDARIC_STUBLIB=\"lib${PACKAGE_NAME}$stub{TCL_TRIM_DOTS}\""
], [
	eval "LAMBDARIC_LIBRARY=\"lib${PACKAGE_NAME}${PACKAGE_VERSION}\""
	eval "LAMBDARIC_STUBLIB=\"lib${PACKAGE_NAME}stub${PACKAGE_VERSION}\""
])
LAMBDARIC_HEADER="lambdaric.h"

AC_DEFINE_UNQUOTED(LAMBDARIC_INCLUDE_PATH_INSTALL, ["$LAMBDARIC_INCLUDE_PATH_INSTALL"])
AC_DEFINE_UNQUOTED(LAMBDARIC_LIBRARY_PATH_INSTALL, ["$LAMBDARIC_LIBRARY_PATH_INSTALL"])
AC_DEFINE_UNQUOTED(LAMBDARIC_PACKAGE_PATH_INSTALL, ["$LAMBDARIC_PACKAGE_PATH_INSTALL"])
AC_DEFINE_UNQUOTED(LAMBDARIC_LIBRARY, ["$LAMBDARIC_LIBRARY"])
AC_DEFINE_UNQUOTED(LAMBDARIC_STUBLIB, ["$LAMBDARIC_STUBLIB"])
AC_DEFINE_UNQUOTED(LAMBDARIC_HEADER, ["$LAMBDARIC_HEADER"])

AC_SUBST(LAMBDARIC_INCLUDE_PATH_INSTALL)
AC_SUBST(LAMBDARIC_LIBRARY_PATH_INSTALL)
AC_SUBST(LAMBDARIC_PACKAGE_PATH_INSTALL)
AC_SUBST(LAMBDARIC_LIBRARY)
AC_SUBST(LAMBDARIC_STUBLIB)
AC_SUBST(LAMBDARIC_HEADER)

AC_CONFIG_FILES([Makefile Makefile.teabase pkgIndex.tcl lambdaricConfig.sh generic/lambdaric.h])
#AC_CONFIG_FILES([sampleConfig.sh])

#--------------------------------------------------------------------
# Finally, substitute all of the various values into the files
# specified with AC_CONFIG_FILES.
#--------------------------------------------------------------------

AC_OUTPUT()
