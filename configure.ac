#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(dnx, 0.17, dnx-devel@lists.sourceforge.net)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([client/dnxWLM.c])
AC_CONFIG_HEADER([config.h])
AC_PREFIX_DEFAULT([/usr/local/nagios])
AC_PREFIX_PROGRAM([nagios])

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([gcc_s], [_Unwind_Backtrace])
AC_CHECK_LIB([rt], [nanosleep])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/file.h sys/socket.h sys/time.h unistd.h getopt.h])

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

# Determine target version of Nagios
AC_ARG_WITH([nagios3x],
	    [AS_HELP_STRING([--with-nagios3x], 
			    [build against nagios 3.x @<:@default=nagios 2.x@:>@])],
	    [nagios_target=3.x],
	    [nagios_target=2.x])
AC_SUBST(nagios_target)

# Determine Nagios user and group
AC_ARG_WITH([nagios-user],
	    [AS_HELP_STRING([--with-nagios-user], 
			    [specify the nagios user @<:@default=nagios@:>@])],
	    [], [with_nagios_user=nagios])
nagios_user="$with_nagios_user"
AC_SUBST(nagios_user)

AC_ARG_WITH([nagios-group],
	    [AS_HELP_STRING([--with-nagios-group], 
			    [specify the nagios group @<:@default=nagios@:>@])],
	    [], [with_nagios_group=nagios])
nagios_group="$with_nagios_group"
AC_SUBST(nagios_group)

# Determine DNX user and group
AC_ARG_WITH([dnx-user],
	    [AS_HELP_STRING([--with-dnx-user], 
			    [specify the dnx client user @<:@default=nagios@:>@])],
	    [], [with_dnx_user=nagios])
dnx_user="$with_dnx_user"
AC_SUBST(dnx_user)

AC_ARG_WITH([dnx-group],
	    [AS_HELP_STRING([--with-dnx-group], 
			    [specify the dnx client group @<:@default=nagios@:>@])],
	    [], [with_dnx_group=nagios])
dnx_group="$with_dnx_group"
AC_SUBST(dnx_group)

# Determine DNX lock/pid file directory
AC_ARG_WITH([run-dir],
	    [AS_HELP_STRING([--with-run-dir], 
			    [specify the dnx client system run directory @<:@default="${localstatedir}/run/${PACKAGE}"@:>@])],
	    [], [with_run_dir="${localstatedir}/run/${PACKAGE}"])
sysrundir="$with_run_dir"
AC_SUBST(sysrundir)

# Determine DNX log/debug file directory
AC_ARG_WITH([log-dir],
	    [AS_HELP_STRING([--with-log-dir], 
			    [specify the dnx system log directory @<:@default="${localstatedir}/log"@:>@])],
	    [], [with_log_dir="${localstatedir}/log"])
syslogdir="$with_log_dir"
AC_SUBST(syslogdir)

# Determine the system init.d directory
AC_ARG_WITH([init-dir],
	    [AS_HELP_STRING([--with-init-dir], 
			    [specify the system init script directory @<:@default="${sysconfdir}/init.d"@:>@])],
	    [], [with_init_dir="${sysconfdir}/init.d"])
initrddir="$with_init_dir"
AC_SUBST(initrddir)

AC_ARG_ENABLE([debug-heap], 
	      [AS_HELP_STRING([--enable-debug-heap], [Turn on DNX debug heap support @<:@default is OFF@:>@])],
	      [case "${enableval}" in
		 yes) dbgheap=yes ;;
		 no)  dbgheap=no  ;;
		 *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug-heap]) ;;
	       esac], [dbgheap=no])
AM_CONDITIONAL([ENABLE_DEBUG_HEAP], [test "${dbgheap}" = yes])
if test "${dbgheap}" = yes; then
  AC_DEFINE([DEBUG_HEAP], 1, [Define to 1 if heap debugging is desired.])
fi

AC_ARG_ENABLE([debug-locks], 
	      [AS_HELP_STRING([--enable-debug-locks], [Turn on DNX debug lock support @<:@default is OFF@:>@])],
	      [case "${enableval}" in
		 yes) dbglocks=yes ;;
		 no)  dbglocks=no  ;;
		 *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug-locks]) ;;
	       esac], [dbglocks=no])
AM_CONDITIONAL([ENABLE_DEBUG_LOCKS], [test "${dbglocks}" = yes])
if test "${dbglocks}" = yes; then
  AC_DEFINE([DEBUG_LOCKS], 1, [Define to 1 if lock debugging is desired.])
fi

AC_CONFIG_FILES([Makefile
		 etc/Makefile
		 obs/dnx.spec
		 obs/dnx.dsc
		 obs/debian.rules
		 obs/debian.control
		 obs/debian.changelog
		 obs/Makefile
		 doc/Makefile
		 doc/doxyfile
		 common/Makefile
		 server/Makefile
		 stats/Makefile
		 client/Makefile
		 client/test/Makefile])
AC_OUTPUT

echo "------------------------------------------------------------
  Project ${PACKAGE} version ${VERSION} configuration:"

echo "
  Targeting Nagios version ${nagios_target}.

  Nagios user is '${nagios_user}'.
  Nagios group is '${nagios_group}'.
  DNX client user is '${dnx_user}'.
  DNX client group is '${dnx_group}'.

  Path prefix is '${prefix}'.
  Data directory is '${datadir}'.
  Log directory is '${syslogdir}'.
  Config directory is '${sysconfdir}'.
  System run path is '${sysrundir}'.
  System init directory is '${initrddir}'.
  Compiler: '${CC} ${DEFS} ${CPPFLAGS} ${CFLAGS}'."

if test "${dbgheap}" = yes; then
  echo "  Debug Heap ENABLED."
fi
if test "${dbglocks}" = yes; then
  echo "  Debug Locks ENABLED."
fi

echo "
  Now type 'make @<:@prefix=<user-prefix>@:>@ @<:@<target>@:>@'
    where the optional <target> is one or more of:
      all                - build all binaries (default).
      docs               - build doxygen documentation.
      install            - install all binaries and data.
      install-client     - install client binaries and data.
      install-server     - install server binaries and data.
      install-cfg        - install both sample config scripts.
      install-clientcfg  - install client sample config script.
      install-servercfg  - install server sample config script.
      install-initscript - install system init script.
      install-docs       - install all documentation."

echo "
    additionally, on rpm-based systems only:
      rpmcheck           - check whether rpms may be built.
      srcrpm             - build only source rpm.
      rpms               - build source and binary rpms."

echo "------------------------------------------------------------"

