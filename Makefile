NAME = $(shell grep '^Package:' control/control | cut -d' ' -f2)
ARCH = $(shell grep '^Architecture:' control/control | cut -d' ' -f2)
VERSION = $(shell grep '^Version:' control/control | cut -d' ' -f2)
IPK_NAME = $(NAME)_$(VERSION)_$(ARCH).ipk

all:
	@echo "Preparing to build $(IPK_NAME)..."
	chmod +x control/postinst
	chmod +x control/prerm
	mkdir -p ipk
	echo "2.0" > ipk/debian-binary
	cp -r control ipk/
	cp -r data ipk/
	cd ipk/control && tar --owner=0 --group=0 -czvf ../control.tar.gz .
	cd ipk/data && tar --owner=0 --group=0 -czvf ../data.tar.gz .
	cd ipk && tar --owner=0 --group=0 -czvf $(IPK_NAME) ./control.tar.gz ./data.tar.gz ./debian-binary
	@echo "$(IPK_NAME) created successfully."

clean:
	@echo "Cleaning up..."
	rm -rf ipk
