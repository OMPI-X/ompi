# -*- shell-script -*-
#
# Copyright (c) 2018      UT-Battelle, LLC
#                         All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

# check if SICM support can be found. 
AC_DEFUN([MCA_opal_mpool_sicm_CONFIG],
         [OPAL_VAR_SCOPE_PUSH([opal_mpool_sicm_happy])
          AC_CONFIG_FILES([opal/mca/mpool/sicm/Makefile])

          _sicm_cppflags=""
          _sicm_ldflags=""
          _sicm_libs=""

          AC_ARG_WITH([sicm],
                      [AC_HELP_STRING([--with-sicm(=DIR)],
                                      [Build with Simple Interface Complex Memory library support])
                      ]
                     )
          OPAL_CHECK_WITHDIR([sicm], [$with_sicm], [include/sicm_low.h])

          AS_IF([test "$with_sicm" != "no"],
                [# At the moment, we always assume that users will use their own installation of SICM

                 AS_IF([test ! -d "$with_sicm"],
                       [AC_MSG_RESULT([not found])
                        AC_MSG_WARN([Directory $with_sicm not found])
                        AC_MSG_ERROR([Cannot continue])
                       ],
                       [AC_MSG_RESULT([found])
                        _sicm_cppflags="-I$with_sicm/include"
                        _sicm_ldflags="-L$with_sicm/lib"
                        _sicm_libs="-lsicm"
                        opal_mpool_sicm_happy="yes"
                       ]
                      )

                ],[opal_mpool_sicm_happy=no]
           )

           AC_SUBST([mpool_sicm_CPPFLAGS],"$_sicm_cppflags")
           AC_SUBST([mpool_sicm_LDFLAGS],"$_sicm_ldflags -lsicm")
           AC_SUBST([mpool_sicm_LIBS],"$_sicm_libs")
           OPAL_VAR_SCOPE_POP
          ]
)
