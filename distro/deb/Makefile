BASEDIR := ../..
PREFIX  := /usr
HEADER  := $(BASEDIR)/gmqcc.h
MAJOR   := `sed -n -e '/GMQCC_VERSION_MAJOR/{s/.* .* //;p;q;}' $(HEADER)`
MINOR   := `sed -n -e '/GMQCC_VERSION_MINOR/{s/.* .* //;p;q;}' $(HEADER)`
PATCH   := `sed -n -e '/GMQCC_VERSION_PATCH/{s/.* .* //;p;q;}' $(HEADER)`
DEBDIR  := gmqcc-$(MAJOR).$(MINOR).$(PATCH)
DEB     := $(DEBDIR).deb

base:
	$(MAKE) -C $(BASEDIR) DESTDIR=distro/deb/$(DEBDIR) PREFIX=$(PREFIX) install
	@install -d -m755 $(DEBDIR)/DEBIAN
	@cp       control $(DEBDIR)/DEBIAN/control
	@tar czf data.tar.gz -C $(DEBDIR)/ . --exclude=DEBIAN
	@tar czf control.tar.gz -C $(DEBDIR)/DEBIAN/ .
	@echo 2.0 > debian-binary
	@ar r $(DEB) debian-binary control.tar.gz data.tar.gz
	@rm -rf debian-binary control.tar.gz data.tar.gz $(DEBDIR)
clean:
	@rm -f $(DEB)


all: base