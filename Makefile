#
# Makefile for fetchlog
#
# $Id: Makefile,v 1.9 2010/07/01 16:39:24 afrika Exp $
#
# 23 Nov 2003
#
# Alexander Haderer 
#
# alexander.haderer@loescap.de
#

#
# distribution version number
#
FETCHLOG_VERSION=1.4

### ------------------------------------------------------------------------
### user settings
### ------------------------------------------------------------------------

###
### compiler

### gcc
CC=gcc
CFLAGS= -O -Werror -Wall -Wcast-qual -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations -Winline -Wcast-align 
### cc
#CC=cc
#CFLAGS=-O 

###
### file compressor (only needed when generating new dists)
FILE_COMP=gzip
#FILE_COMP=compress

###
### comment the line below if 
### 1. compilation fails with 'MADV_RANDOM' undeclared'
###    (This will most probably happen on Linux 2.2 systems)
### or
### 2. fetchlog fails witch  'ERROR: fetchlog: madvise: Invalid argument'
###    (This will most probably happen on SGI's IRIX using cc or gcc)
HAS_MADVISE=-DHAS_MADVISE

###
### comment the line below if compilation fails because of a missing
### regex functions. Errors are like
###     compiler: "fetchlog.c: 45: Can't find include file regex.h"
###     compiler: "fetchlog.c: unknown symbol REG_EXTENDED / REG_NOSUB"
###     linker:   "unknown symbol regexec/regcomp/regerror"
###
HAS_REGEX=-DHAS_REGEX

###
### installdir    
###   will use $INSTDIR/bin  and  $INSTDIR/man/man1, 
###   both dirs have to exist for installation
INSTDIR=/usr/local

### ------------------------------------------------------------------------
### --- end of user settings -----------------------------------------------
### ------------------------------------------------------------------------

# all files of this dist
# ----------------------
MYFILES= Makefile LICENSE CHANGES README README.SNMP README.Nagios \
	fetchlog-Makefile.hpux fetchlog.psf \
	fetchlog.cfg fetchlog_service.cfg notify.cfg.example \
	test-all fetchlog.c fetchlog.1

# compiler stuff
# --------------
CC_OPT= $(CFLAGS) -DFETCHLOG_VERSION_NO=\"$(FETCHLOG_VERSION)\" \
	$(HAS_MADVISE) $(HAS_REGEX)

# all
# ---
all: fetchlog

# fetchlog
# -------
fetchlog: fetchlog.c 
	$(CC) $(CC_OPT) fetchlog.c -o fetchlog


# install
# -------
install: fetchlog
	strip fetchlog
	cp fetchlog $(INSTDIR)/bin
	chmod 0755 $(INSTDIR)/bin/fetchlog
	cp fetchlog.1 $(INSTDIR)/man/man1
	chmod 0444 $(INSTDIR)/man/man1/fetchlog.1

# test
# ----
test: fetchlog
	@echo "testing basic fetchlog:"
	@echo > ./testfile "--> basic fetchlog works!"
	@./fetchlog -f 1:100:500: `pwd`/testfile /tmp/dummy/bookmark || true
	@echo "testing regex fetchlog:"
	@echo > ./testfile "--> regex fetchlog works!"
	@./fetchlog -f 1:100:500: `pwd`/testfile /tmp/dummy/bookmark 't'|| true
	@echo "testing completed"
	@rm ./testfile

# testall
# -------
testall:
	@echo "now testing all fetchlog features"
	@./test-all

# clean
# -----
clean:
	rm -f *~ core *.core *.o fetchlog

# dist
# ----
dist:
	rm -rf fetchlog-$(FETCHLOG_VERSION)
	mkdir fetchlog-$(FETCHLOG_VERSION)
	cp $(MYFILES) fetchlog-$(FETCHLOG_VERSION)
	tar cf fetchlog-$(FETCHLOG_VERSION).tar \
		fetchlog-$(FETCHLOG_VERSION)
	rm -rf fetchlog-$(FETCHLOG_VERSION)
	$(FILE_COMP) fetchlog-$(FETCHLOG_VERSION).tar
