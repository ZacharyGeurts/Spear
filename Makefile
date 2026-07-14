# SPDX-License-Identifier: MIT
# Spear — full stack to the OS · ISO release
# Product path: C++ and lower. Scripts are build helpers only (not runtime commander).
.PHONY: all stack install wartime initrd pack product field-iso iso iso-stamp release receipt clean help

ROOT := $(abspath .)
export SPEAR_ROOT := $(ROOT)
VERSION := $(shell cat $(ROOT)/VERSION 2>/dev/null || echo 22.3.1-field)

help:
	@echo "Spear $(VERSION) — Field product = test boot drive stack"
	@echo "  make product     PRODUCT Field ISO (no Mint underlay · no casper)"
	@echo "  make all|stack   build C++ ELFs"
	@echo "  make install     install ELFs → overlay + ~/.local/bin"
	@echo "  make initrd      field initramfs"
	@echo "  make pack        product boot disk out/spear-boot.img"
	@echo "  make iso         optional Mint casper (NOT product)"
	@echo "  make release     = make product"
	@echo "  make clean       remove built ELFs"

all stack wartime:
	$(MAKE) -C src all

install: all
	@bash scripts/install-stack.sh

initrd: all
	@bash boot/build-initrd.sh "$(ROOT)/out/initramfs.cpio.gz"

pack: initrd
	@bash boot/pack.sh

# Product of record — same path as Field1 test boot drive (no casper/Mint underlay)
product field-iso: install initrd
	@bash boot/make-field-iso.sh
	@bash scripts/release-receipt.sh "$$(readlink -f $(ROOT)/out/spear-field-latest.iso 2>/dev/null || true)" || true
	@echo "PRODUCT Field ISO ready (not Mint underlay) · docs/PRODUCT-BOOT.md"

# Optional: Mint casper utility live only — NOT the stack of record
iso:
	@echo "NOTE: casper Mint remaster is optional utility — product is: make product"
	@bash iso/build-all.sh

iso-stamp: install
	@echo "NOTE: casper stamp is optional — product is: make product"
	@bash iso/apply-stack.sh
	@bash iso/rebuild-iso.sh

receipt:
	@bash scripts/release-receipt.sh

release: product
	@echo "RELEASE ready · version $(VERSION) · Field product ISO · docs/PRODUCT-BOOT.md"

clean:
	$(MAKE) -C src clean
