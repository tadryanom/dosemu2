top_builddir=..
include $(top_builddir)/Makefile.conf

LANGUAGES = ru
vpath %.1.in $(top_srcdir)/man

MANPAGES = dosemu.1 dosemu.bin.1 ru/dosemu.1 ru/dosemu.bin.1

default all: $(MANPAGES)

.NOTPARALLEL: $(MANPAGES)
$(MANPAGES): $(top_builddir)/config.status $(wildcard $(top_srcdir)/man/*.1.in) \
    $(wildcard $(top_srcdir)/man/ru/*.1.in)
	cd $(top_builddir) && ./config.status

install: all
	$(INSTALL) -d $(DESTDIR)$(mandir)/man1
	$(INSTALL) -m 0644 *.1 $(DESTDIR)$(mandir)/man1
	for i in $(LANGUAGES); do \
	    $(INSTALL) -d $(DESTDIR)$(mandir)/$$i/man1; \
	    $(INSTALL) -m 0644 $$i/*.1 $(DESTDIR)$(mandir)/$$i/man1; \
	    $(INSTALL) -m 0644 $(REALTOPDIR)/man/$$i/*.1 \
	      $(DESTDIR)$(mandir)/$$i/man1; \
	done

clean realclean:
	for i in `ls $(top_srcdir)/man/*.1.in $(top_srcdir)/man/ru/*.1.in` ; do \
	    rm -f `echo $$i | sed -s -e 's/\.in$$//' \
		    -e 's=$(top_srcdir)/man/=='`; \
	done
