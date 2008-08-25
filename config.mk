# libebb version
VERSION = 0.1

# Customize below to fit your system

# paths
PREFIX = $(HOME)/local
MANPREFIX = ${PREFIX}/share/man

# libev
EVINC = $(HOME)/local/libev/include
EVLIB = $(HOME)/local/libev/lib

# GnuTLS, comment if you don't want it (necessary for HTTPS)
GNUTLSLIB   = /usr/lib
GNUTLSINC   = /usr/include
GNUTLSLIBS  = -L${GNUTLSLIB} -lgnutls
GNUTLSFLAGS = -DHAVE_GNUTLS

# includes and libs
INCS = -I. -I/usr/include -I${EVINC} -I${GNUTLSINC}
LIBS = -L/usr/lib -lev -L${EVLIB} ${GNUTLSLIBS}

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\" ${GNUTLSFLAGS}
CFLAGS   = -Wall ${INCS} ${CPPFLAGS} -fPIC
LDFLAGS  = -s ${LIBS}
LDOPT    = -shared
SUFFIX   = so
SONAME   = -Wl,-soname,libptr_array-$(VERSION).$(SUFFIX)

# Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\" -fPIC
#LDFLAGS = ${LIBS}
#SONAME = 

# Darwin
# LDOPT=-dynamiclib 
# SUFFIX=dylib
# SONAME=-current_version $(VERSION) -compatibility_version $(VERSION)

# compiler and linker
CC = cc
RANLIB = ranlib
