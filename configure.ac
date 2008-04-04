#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(dnx, 0.15, dnx-devel@lists.sourceforge.net)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([client/dnxWLM.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_MAKE_SET
AC_CHECK_PROG(DOXYGEN_INSTALLED, doxygen, yes)
AM_CONDITIONAL(DO_DOXYGEN, [test x$DOXYGEN_INSTALLED = xyes])

AC_ARG_WITH([nagios3x],
	    [AS_HELP_STRING([--with-nagios3x], 
			    [build against nagios 3.x (default: nagios 2.x)])],
	    [nagios_target=3.x],
	    [nagios_target=2.x])

AC_SUBST(nagios_target)

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([gcc_s], [_Unwind_Backtrace])
AC_CHECK_LIB([rt], [nanosleep])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/file.h sys/socket.h sys/time.h syslog.h unistd.h getopt.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_GETPGRP
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STRTOD
AC_CHECK_FUNCS([dup2 gethostbyname gettimeofday memmove memset regcomp \
   nanosleep select socket strchr strdup strerror strrchr strtol strtoul \
   getopt])

# Check for getopt_long
AC_SEARCH_LIBS([getopt_long],[iberty],[AC_CHECK_FUNCS([getopt_long])])

# Export RPM build architecture
which rpm > /dev/null; \
if [[ $? -eq 0 ]]; then \
   BUILDARCH=`rpm --showrc | grep ^build\ arch | sed 's/\(.*: \)\(.*\)/\2/'`; \
else \
   BUILDARCH=$build_cpu; \
fi
AC_SUBST(BUILDARCH)
AC_DEFINE_UNQUOTED(BUILDARCH, ["$BUILDARCH"],
	[The build architecture of this build.])

AC_CONFIG_FILES([Makefile
		 rpm/dnx.spec
                 deb/dnx.dsc.in
                 deb/debian.changelog.in
		 doc/Makefile
		 doc/doxyfile
		 etc/Makefile
		 etc/dnxcld.init
		 common/Makefile
		 server/Makefile
		 client/Makefile
		 stats/Makefile])
AC_OUTPUT
