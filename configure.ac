dnl Process this file with autoconf to produce a configure script.

AC_INIT(libolmec, 0.1)

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11])

AM_SILENT_RULES([yes])

AC_PROG_CC

AC_CONFIG_MACRO_DIR([m4])

dnl ***************************************************************************
dnl Internationalization
dnl ***************************************************************************
IT_PROG_INTLTOOL([0.35.0])

GETTEXT_PACKAGE=libolmec
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])
AM_GLIB_GNU_GETTEXT

LT_INIT

PKG_CHECK_MODULES(libxml, [libxml-2.0 > 2.4])
AC_SUBST(libxml_CFLAGS)
AC_SUBST(libxml_LIBS)

AC_CHECK_LIB([simclist], [list_init], [], [AC_MSG_ERROR([libsimclist not found (you can get it from https://github.com/mij/simclist)])])

AC_OUTPUT([
Makefile
src/libolmec-0.1.pc
src/Makefile
man/Makefile
po/Makefile.in])
