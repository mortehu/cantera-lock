bin_PROGRAMS = cantera-lock

CONVERT = @CONVERT@

AM_CFLAGS = -g -Wall -pthread $(PACKAGES_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS)

cantera_lock_SOURCES = \
  font.c font.h \
  glyph.c glyph.h \
  draw.h draw.c \
  main.c \
  lock.c
cantera_lock_LDADD = $(PACKAGES_LIBS) -lm -lGL -lXinerama -lcrypt
