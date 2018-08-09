#
# Copyright (c) 2018    UT-Battelle, LLC.
#                       All rights reserved.
#
# Additional copyrights may follow
#
# $HEADER$
#

dist_ortedata_DATA = help-orte-rmaps-explicit.txt

sources = \
        rmaps_explicit.c \
        rmaps_explicit_internals.c \
        rmaps_explicit.h \
        rmaps_explicit_component.c

if MCA_BUILD_orte_rmaps_explicit_DSO
component_noinst =
component_install = mca_rmaps_explicit.la
else
component_noinst = libmca_rmaps.explicit.la
component_install =
endif

mcacomponentdir = $(ortelibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_rmaps_explicit_la_SOURCES = $(sources)
mca_rmaps_explicit_la_LDFLAGS = -module -avoid-version
mca_rmaps_explicit_la_LIBADD = $(top_builddir)/orte/lib@ORTE_LIB_PREFIX@open-rte.la

noinst_LTLIBRARIES = $(component_noinst)
libmca_rmaps_explicit_la_SOURCES = $(sources)
libmca_rmaps_explicit_la_LDFLAGS = -module -avoid-version
