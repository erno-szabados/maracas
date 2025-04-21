# configure.ac
AC_INIT([Maracas], [0.1.0], [erno.szabados@windowslive.com])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_PROG_CC 

PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= 3.22])
PKG_CHECK_MODULES([PULSE], [libpulse >= 1.0]) # Adjust version if needed

AC_SUBST([GTK_CFLAGS])
AC_SUBST([GTK_LIBS])
AC_SUBST([PULSE_CFLAGS])
AC_SUBST([PULSE_LIBS])

AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_OUTPUT
