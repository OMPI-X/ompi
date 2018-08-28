# -*- indent-tabs-mode:nil -*-
#
# Copyright (c)		2018	UT-Battelle, LLC
# 				All rights reserved.
#
# Additional copyrights may follow
#
# $HEADERS$
#


AM_CPPFLAGS = $(mpool_sicm_CPPFLAGS)
AM_LDFLAGS = $(mpool_sicm_LDFLAGS)

if MCA_BUILD_opal_mpool_sicm_DSO
component_noinst =
component_install = mca_mpool_sicm.la
else
component_noinst = libmca_mpool_sicm.la
component_install = 
endif

sicm_SOURCES = mpool_sicm.h \
	       mpool_sicm_component.c \
	       mpool_sicm_module.c

mcacomponentdir = $(opallibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_mpool_sicm_la_SOURCES = $(sicm_SOURCES)
nodist_mca_mpool_sicm_la_SOURCES = $(sicm_nodist_SOURCES)
mca_mpool_sicm_la_LIBASS = $(top_builddir)/opal/lib@OPAL_LIB_PREFIX@open-pal.la \
			   $(mpool_sicm_LIBS)
mca_mpool_sicm_la_LDFLAGS = -module -avoid-version $(mpool_sicm_LDFLAGS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_mpool_sicm_la_SOURCES = $(sicm_SOURCES)
nodist_libmca_mpool_sicm_la_SOURCES = $(sicm_nodist_SOURCES)
libmca_mpool_sicm_la_LIBADD = $(mpool_sicm_LIBS)
libmca_mpool_sicm_la_LDFLAGS = -module -avoid-version $(mpool_sicm_LDFLAGS)
