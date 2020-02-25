include $(top_srcdir)/flymake.mk

AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
AM_CFLAGS = $(AM_CPPFLAGS)
LDADD = $(top_builddir)/src/libzeroskip.la

# Define _XOPEN_SOURCE
AM_CFLAGS += -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE
if IS_DARWIN
AM_CFLAGS += -DMACOSX -DDARWIN -D_BSD_SOURCE
endif
if IS_DRAGONFLY
AM_CFLAGS += -DDRAGONFLY
endif
if IS_LINUX
AM_CFLAGS += -DLINUX -D_DEFAULT_SOURCE -D_GNU_SOURCE
endif
if IS_FREEBSD
AM_CFLAGS += -DFREEBSD
endif
if IS_NETBSD
AM_CFLAGS += -DNETBSD
endif
if IS_OPENBSD
AM_CFLAGS += -DOPENBSD
endif
if IS_SUNOS
AM_CFLAGS += -DSUNOS -DSOLARIS
endif

# Enable debug flags
if IS_DEBUG
AM_CFLAGS += -ggdb
else
AM_CFLAGS += -ggdb -O2
endif


bin_PROGRAMS =  \
	zsbench \
	zsmany  \
	crc32bench

zsbench_SOURCES = \
	zsbench.c

zsmany_SOURCES = \
	zsmany.c

crc32bench_SOURCES = \
	crc32bench.c

crc32bench_CFLAGS = $(LIBZLIB_CFLAGS) $(AM_CFLAGS)
crc32bench_LDFLAGS = $(LIBZLIB_LIBS)
