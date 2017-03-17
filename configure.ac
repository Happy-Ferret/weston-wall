AC_PREREQ([2.65])
AC_INIT([weston-wall],[0])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_SRCDIR([src/notification-area.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.aux])

AM_INIT_AUTOMAKE([1.14 foreign subdir-objects parallel-tests dist-xz no-dist-gzip tar-ustar])
AM_SILENT_RULES([yes])

AC_ARG_VAR([WAYLAND_SCANNER], [Wayland XML scanner executable])

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_MKDIR_P
LT_INIT([disable-static pic-only])
PKG_PROG_PKG_CONFIG
AC_PATH_PROG([WAYLAND_SCANNER], [wayland-scanner])
if test -z "${WAYLAND_SCANNER}"; then
    AC_MSG_ERROR([wayland-scanner not found])
fi
AC_PROG_INSTALL

AC_DEFINE([WW_EXPORT], [__attribute__((visibility("default")))], [Export symbol])

AM_CFLAGS="-fvisibility=hidden -Wall -Wextra -Wformat=2 -Wno-unused-parameter -Wno-missing-field-initializers"
AM_CPPFLAGS=""
AM_LDFLAGS=""

AC_DEFUN([WW_CHECK_WESTON], [
    m4_ifnblank([$1], [
        m4_ifblank([$2], [
            PKG_CHECK_MODULES([WESTON], [libweston-$1 >= ${weston_min_version}])
        ], [
            PKG_CHECK_MODULES([WESTON], [libweston-$1 >= ${weston_min_version}], [], [WW_CHECK_WESTON(m4_shift($@))])
        ])
    ])
])

#
# Dependencies
#
m4_define([weston_supported_majors], [3, 2])
weston_min_version=1.12.90
WW_CHECK_WESTON(weston_supported_majors)
PKG_CHECK_MODULES([WAYLAND_WALL], [wayland-wall])
PKG_CHECK_MODULES([WAYLAND], [wayland-server])

AC_ARG_WITH([westonmoduledir], AS_HELP_STRING([--with-westonmoduledir=DIR], [Directory for Weston plugins]), [], [with_westonmoduledir=yes])
case "${with_westonmoduledir}" in
    no|"") AC_MSG_ERROR([*** You must define westonmoduledir when compiling Weston plugin]) ;;
    yes) westonmoduledir="`${PKG_CONFIG} --variable=libdir weston`/weston" ;;
    *) westonmoduledir="${with_westonmoduledir}" ;;
esac
AC_SUBST([westonmoduledir])

wwprotocoldir=`${PKG_CONFIG} --variable=pkgdatadir wayland-wall`
AC_SUBST([wwprotocoldir])


AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_LDFLAGS])

AM_SUBST_NOTMAKE([LIBS])
AC_SUBST([shrext_cmds])

AC_CONFIG_FILES([
    Makefile
])
AC_OUTPUT
