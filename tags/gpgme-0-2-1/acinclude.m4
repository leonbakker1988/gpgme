dnl Macros to configure gpgme


dnl GNUPG_FIX_HDR_VERSION(FILE, NAME)
dnl (wk 2000-11-17)
AC_DEFUN(GNUPG_FIX_HDR_VERSION,
  [ sed "s/^#define $2 \".*/#define $2 \"$VERSION\"/" $srcdir/$1 > $srcdir/$1.tmp
    if cmp -s $srcdir/$1 $srcdir/$1.tmp 2>/dev/null; then
        rm -f $srcdir/$1.tmp
    else
        rm -f $srcdir/$1
        if mv $srcdir/$1.tmp $srcdir/$1 ; then
            :
        else
            AC_MSG_ERROR([[
*** Failed to fix the version string macro $2 in $1.
*** The old file has been saved as $1.tmp
                         ]])
        fi
        AC_MSG_WARN([fixed the $2 macro in $1])
    fi
  ])

dnl GNUPG_CHECK_TYPEDEF(TYPE, HAVE_NAME)
dnl Check whether a typedef exists and create a #define $2 if it exists
dnl
AC_DEFUN(GNUPG_CHECK_TYPEDEF,
  [ AC_MSG_CHECKING(for $1 typedef)
    AC_CACHE_VAL(gnupg_cv_typedef_$1,
    [AC_TRY_COMPILE([#include <stdlib.h>
    #include <sys/types.h>], [
    #undef $1
    int a = sizeof($1);
    ], gnupg_cv_typedef_$1=yes, gnupg_cv_typedef_$1=no )])
    AC_MSG_RESULT($gnupg_cv_typedef_$1)
    if test "$gnupg_cv_typedef_$1" = yes; then
        AC_DEFINE($2)
    fi
  ])




