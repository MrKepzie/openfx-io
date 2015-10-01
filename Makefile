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
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for i in $(SUBDIRS) $(SUBDIRS_NOMULTI); do \
	  $(MAKE) -C $$i $@; \
	done

install:
	for i in $(SUBDIRS) ; do \
	  $(MAKE) -C $$i $@; \
	done

install-nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)" install

uninstall:
	for i in $(SUBDIRS) ; do \
	  $(MAKE) -C $$i $@; \
	done

uninstall-nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)" uninstall
