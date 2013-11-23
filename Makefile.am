bin_PROGRAMS = cantera-lock
pkgdata_DATA = gfx/fonts/vera-sans-mono.dat gfx/fonts/vera-sans-mono_0.png

CONVERT = @CONVERT@

XLDFLAGS =  -L/usr/X11R6/lib -lX11 -lXrender -lXft -lXpm -lpng -ljpeg -lgif
AM_CFLAGS = -g -Wall -pthread
AM_CPPFLAGS = -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64

cantera_lock_SOURCES = \
  draw.h draw.c \
  error.h error.c \
  font.h lock-font.c \
  texture.h texture.c \
  main_x11.c \
  lock.c
cantera_lock_LDADD = -lX11 -lGL -lGLU -lm -lXxf86vm -lpng -lXinerama -lcrypt

EXTRA_DIST = \
  cantera-lock/gfx/fonts/vera-sans-mono.dat \
  cantera-lock/gfx/fonts/vera-sans-mono_0.png
