# libEbb - web server library
# See README file for copyright and license details.

include config.mk

SRC = ebb.c ebb_request_parser.c rbtree.c
OBJ = ${SRC:.c=.o}

NAME=libebb
OUTPUT_LIB=$(NAME).$(VERSION).$(SUFFIX)
OUTPUT_A=$(NAME).a

LINKER=$(CC) $(LDOPT)

all: options $(OUTPUT_LIB) $(OUTPUT_A)

options:
	@echo libebb build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "LDOPT    = ${LDOPT}"
	@echo "SUFFIX   = ${SUFFIX}"
	@echo "SONAME   = ${SONAME}"
	@echo "CC       = ${CC}"

$(OUTPUT_LIB): $(OBJ) 
	$(LINKER) -o $(OUTPUT_LIB) $(OBJ) $(SONAME) $(LIBS)

$(OUTPUT_A): $(OBJ)
	$(AR) cru $(OUTPUT_A) $(OBJ)
	$(RANLIB) $(OUTPUT_A)

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: ebb.h ebb_request_parser.h rbtree.h config.mk

ebb_request_parser.c: ebb_request_parser.rl
	ragel -s -G2 $< -o $@

test: test_request_parser test_rbtree
	time ./test_request_parser
	./test_rbtree

test_rbtree: test_rbtree.o $(OUTPUT_A)
	$(CC) -o $@ $< $(OUTPUT_A)

test_request_parser: test_request_parser.o $(OUTPUT_A)
	$(CC) -o $@ $< $(OUTPUT_A)

examples: examples/hello_world

examples/hello_world: examples/hello_world.o $(OUTPUT_A) 
	$(CC) $(LIBS) $(CFLAGS) -lev -o $@ $< $(OUTPUT_A)

clean:
	@echo cleaning
	@rm -f ${OBJ} libebb-${VERSION}.tar.gz test_rbtree test_request_parser examples/hello_world

clobber: clean
	@echo clobbering
	@rm -f ebb_request_parser.c

dist: clean
	@echo creating dist tarball
	@mkdir -p dwm-${VERSION}
	@cp -R LICENSE Makefile README config.def.h config.mk \
		dwm.1 ${SRC} dwm-${VERSION}
	@tar -cf dwm-${VERSION}.tar dwm-${VERSION}
	@gzip dwm-${VERSION}.tar
	@rm -rf dwm-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dwm ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dwm
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < dwm.1 > ${DESTDIR}${MANPREFIX}/man1/dwm.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dwm.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dwm
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dwm.1

.PHONY: all options clean clobber dist install uninstall test examples
