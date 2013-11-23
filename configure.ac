AC_INIT(cantera-lock,1.1)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_GNU_SOURCE

PKG_CHECK_MODULES([PACKAGES], [freetype2 >= 9.20 fontconfig >= 2.8.0 x11 xinerama xrender])

AC_OUTPUT(Makefile)
