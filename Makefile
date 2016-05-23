# build a combined plugin that reads all formats
SUBDIRS = IO

SUBDIRS_NOMULTI = \
EXR \
FFmpeg \
OCIO \
OIIO \
SeExpr \
PFM

all: subdirs

.PHONY: nomulti subdirs clean install install-nomulti uninstall uninstall-nomulti $(SUBDIRS)

nomulti:
	$(MAKE) $(MFLAGS) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS):
	(cd $@ && $(MAKE) $(MFLAGS))

clean:
	@for i in $(SUBDIRS) $(SUBDIRS_NOMULTI); do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

install:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

install-nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)" install

uninstall:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

uninstall-nomulti:
	$(MAKE) $(MFLAGS) SUBDIRS="$(SUBDIRS_NOMULTI)" uninstall
