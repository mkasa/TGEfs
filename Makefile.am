bin_PROGRAMS = tgefs tgelzo
tgefs_SOURCES = tgefs.cc sha2.cc minilzo.c lzocomp.cc tge_fcopy.cc tge_log.cc tge_compctl.cc tge_cache.cc tge_appconfig.cc config.h lzocomp.h lzoconf.h lzodefs.h minilzo.h pmutex.h sha2.h tge_appconfig.h tge_cache.h tge_compctl.h tge_fcopy.h tge_log.h ppthread.cc ppthread.h socket.h libtgelock.h
tgelzo_SOURCES = tgelzo.cc minilzo.c lzocomp.cc tge_fcopy.cc
EXTRA_DIST = boot.tgefs tgefs.conf tgefscc.conf

AM_CXXFLAGS = -pthread -D_FILE_OFFSET_BITS=64 -O2 -DNDEBUG -Wall
AM_LDFLAGS  = -pthread

install-exec-hook:
	cd $(DESTDIR)$(bindir); test -e lzo   || ln -s ./tgelzo ./lzo
	cd $(DESTDIR)$(bindir); test -e unlzo || ln -s ./tgelzo ./unlzo
	cd $(DESTDIR)$(bindir); test -e lcat  || ln -s ./tgelzo ./lcat

uninstall-hook:
	cd $(DESTDIR)$(bindir); test -L lzo   && rm ./lzo
	cd $(DESTDIR)$(bindir); test -L unlzo && rm ./unlzo
	cd $(DESTDIR)$(bindir); test -L lcat  && rm ./lcat

d:
	-fusermount -u tst
	./tgefs tst
	echo 3 > tst/proc/tgefslog
	cat tst/home/mkasa/work/tge/TGEW.pm
	cat tst/home/mkasa/work/tge/TGEW.pm
	cat tst/home/mkasa/work/tge/TGEW.pm

dd:
	-fusermount -u tst
	./tgefs -d tst

cc:
	rm -i /grid2/mkasa/tgetmp/*

um:
	-fusermount -u tst


