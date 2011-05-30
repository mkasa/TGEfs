#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(tgefs, 1.1)
AM_INIT_AUTOMAKE(dist-bzip2)
AC_CONFIG_HEADER([config.h])
AC_CONFIG_SRCDIR([lzocomp.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB(pthread,pthread_mutex_init,,
    [AC_MSG_ERROR([tge-fs requires pthread.])])
AC_CHECK_LIB(fuse,fuse_main,,
    [AC_MSG_ERROR([tge-fs requires libfuse2.])])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stddef.h stdlib.h string.h sys/file.h sys/time.h sys/vfs.h unistd.h utime.h],,
    [AC_MSG_ERROR([tge-fs requires standard linux headers, which may not be present for other systems. see the manual for details.])])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize],,
    [AC_MSG_ERROR([error. see the manual for details.])])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_rdev],,
    [AC_MSG_ERROR([error. see the manual for details.])])
AC_HEADER_TIME
AC_STRUCT_TM
AC_CHECK_TYPES([ptrdiff_t],,
    [AC_MSG_ERROR([error. see the manual for details.])])
AC_SYS_LARGEFILE

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_UTIME_NULL
# AC_CHECK_FUNCS([clock_gettime gethostname lchown memmove memset mkdir mkfifo realpath rmdir utime],,
AC_CHECK_FUNCS([gethostname lchown memmove memset mkdir mkfifo realpath rmdir utime],,
    [AC_MSG_ERROR([Missing function(s) required for tge-fs build was found. tge-fs requires linux kernel 2.6 or later. see the manual for details.])])
AC_OUTPUT
