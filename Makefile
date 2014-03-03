# build a combined plugin that reads all formats
SUBDIRS = IO
# separate plugins for each format can also be built by uncommenting the following line
#SUBDIRS += FFmpeg EXR OIIO PFM OCIO

all: subdirs

.PHONY: subdirs clean $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean :
	for i in $(SUBDIRS) ; do \
	  $(MAKE) -C $$i clean; \
	done
