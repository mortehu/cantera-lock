AC_INIT(cantera-lock,1.1)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_GNU_SOURCE

PKG_CHECK_MODULES([PACKAGES], [x11 freetype2 >= 9.20 fontconfig >= 2.8.0 glew])

AC_CHECK_HEADER([X11/extensions/Xrender.h], [], [AC_MSG_ERROR([libxrender required (apt-get install libxrender-dev)])])
AC_CHECK_HEADER([X11/extensions/Xinerama.h], [], [AC_MSG_ERROR([libxinerama required (apt-get install libxinerama-dev)])])

AC_C_BIGENDIAN

AC_OUTPUT(Makefile)
