## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir). \
	$(LIBXML_CFLAGS)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\"

AM_CFLAGS =\
	 -Wall\
	 -g

lib_LTLIBRARIES = libolmec.la


libolmec_la_SOURCES = \
	contact.c \
	libolmec.c \
	private.h \
	contact.h

libolmec_la_CFLAGS = $(libxml_CFLAGS) \
	--std=gnu99 

libolmec_la_LDFLAGS =  \
	$(libxml_LIBS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libolmec-0.1.pc \
	libolmec-0.1.pc.in

EXTRA_DIST = \
	libolmec-0.1.pc.in

include_HEADERS = libolmec.h
