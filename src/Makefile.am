## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir). \
	$(LIBXML_CFLAGS)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\"

lib_LTLIBRARIES = libolmec.la

libolmec_la_SOURCES = \
	contact.c \
	libolmec.c \
	private.h \
	contact.h

libolmec_la_CFLAGS = -Wall --std=gnu99 -O3 $(libxml_CFLAGS)

libolmec_la_LDFLAGS = $(libxml_LIBS)

include_HEADERS = libolmec.h
