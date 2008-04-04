#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(dnx, 0.16, dnx-devel@lists.sourceforge.net)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([client/dnxWLM.c])
AC_CONFIG_HEADER([config.h])
AC_PREFIX_DEFAULT([/usr/local/nagios])

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_MAKE_SET

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
			    [specify the dnx client system run directory\
 @<:@default="${localstatedir}/run/${PACKAGE}"@:>@])],
	    [], [with_run_dir="${localstatedir}/run/${PACKAGE}"])
sysrunpath="$with_run_dir"
AC_SUBST(sysrunpath)

# Determine the system init.d directory
AC_ARG_WITH([init-dir],
	    [AS_HELP_STRING([--with-init-dir], 
			    [specify the system init script directory\
 @<:@default="${sysconfdir}/init.d"@:>@])],
	    [], [with_init_dir="${sysconfdir}/init.d"])
initrddir="$with_init_dir"
AC_SUBST(initrddir)

# Export RPM build architecture
which rpm > /dev/null; \
if [[ $? -eq 0 ]]; then \
   BUILDARCH=`rpm --showrc | grep ^build\ arch | sed 's/\(.*: \)\(.*\)/\2/'`; \
else \
   BUILDARCH="$build_cpu"; \
fi
AC_SUBST(BUILDARCH)
AC_DEFINE_UNQUOTED(BUILDARCH, ["$BUILDARCH"],
	[The build architecture of this build.])

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

echo "------------------------------------------------------------"
echo
echo "  Project ${PACKAGE} version ${VERSION} configuration:"
echo
echo "  Targeting Nagios version ${nagios_target}."
echo
echo "  Nagios user is '${nagios_user}'."
echo "  Nagios group is '${nagios_group}'."
echo "  DNX client user is '${dnx_user}'."
echo "  DNX client group is '${dnx_group}'."
echo
echo "  Path prefix is '${prefix}'."
echo "  Data directory is '${datadir}'."
echo "  Config directory is '${sysconfdir}'."
echo "  Client run path is '${sysrunpath}'."
echo "  Local state directory is '${localstatedir}'."
echo "  System init directory is '${initrddir}'."
echo
echo "  Now type 'make @<:@prefix=<user-prefix>@:>@ @<:@<target>@:>@'"
echo "    where the optional <target> is:"
echo "      all                - build all binaries (default)."
echo "      docs               - build doxygen documentation."
echo "      install            - install all binaries and data."
echo "      install-client     - install client binaries and data."
echo "      install-server     - install server binaries and data."
echo "      install-cfg        - install both sample config scripts."
echo "      install-clientcfg  - install client sample config script."
echo "      install-servercfg  - install server sample config script."
echo "      install-initscript - install system init script."
echo "      install-docs       - install all documentation."
echo
echo "    additionally on rpm-based systems only:"
echo "      rpmcheck           - check whether rpms may be built."
echo "      srcrpm             - build only source rpm."
echo "      rpms               - build source and binary rpms."
echo
echo "------------------------------------------------------------"

