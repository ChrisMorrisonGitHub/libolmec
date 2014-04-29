## Process this file with automake to produce Makefile.in

SUBDIRS = src po man

libolmecdocdir = ${prefix}/doc/libolmec
libolmecdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libolmec.pc

INTLTOOL_FILES = intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

EXTRA_DIST = $(libolmecdoc_DATA) \
	$(INTLTOOL_FILES)

DISTCLEANFILES = intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(libolmecdocdir)
