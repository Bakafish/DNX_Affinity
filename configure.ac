#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(dnx, 0.15, dnx-devel@lists.sourceforge.net)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([client/dnxWLM.c])
AC_CONFIG_HEADER([config.h])
AC_PREFIX_DEFAULT([/usr/local/nagios])

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_MAKE_SET

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
		 etc/Makefile
		 obs/dnx.spec
		 obs/dnx.dsc
		 obs/debian.changelog
		 obs/Makefile
		 doc/Makefile
		 doc/doxyfile
		 common/Makefile
		 server/Makefile
		 client/Makefile
		 stats/Makefile])
AC_OUTPUT

echo "------------------------------------------------------------"
echo
echo "  Project ${PACKAGE} version ${VERSION} configuration:"
echo
echo "  Prefix is currently '${prefix}'."
echo
echo "  Targeting Nagios version ${nagios_target}."
echo
echo "  Now type 'make @<:@prefix=<user-prefix>@:>@ @<:@<target>@:>@'"
echo
echo "    where the optional <target> is:"
echo
echo "      all                - build all binaries (default)."
echo "      docs               - build doxygen documentation."
echo "      install            - install all binaries and data."
echo "      install-cfg        - install sample config scripts."
echo "      install-docs       - install all documentation."
echo "      install-initscript - install system init script."
echo
echo "    additionally on rpm-based systems only:"
echo
echo "      rpmcheck           - check whether rpms may be built."
echo "      srcrpm             - build only source rpm."
echo "      rpms               - build source and binary rpms."
echo
echo "------------------------------------------------------------"

