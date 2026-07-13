# SPDX-License-Identifier: MIT
# Spear — full stack to the OS · ISO release
# Product path: C++ and lower. Scripts are build helpers only (not runtime commander).
.PHONY: all stack install wartime initrd pack iso iso-stamp release receipt clean help

ROOT := $(abspath .)
export SPEAR_ROOT := $(ROOT)
VERSION := $(shell cat $(ROOT)/VERSION 2>/dev/null || echo 22.3.1-field)

help:
	@echo "Spear $(VERSION) — full stack"
	@echo "  make all|stack   build C++ ELFs"
	@echo "  make install     install ELFs → overlay + ~/.local/bin"
	@echo "  make initrd      field initramfs (out/initramfs.cpio.gz)"
	@echo "  make pack        limine product image (self-contained out/)"
	@echo "  make iso         full remaster: fetch → extract → apply → rebuild ISO"
	@echo "  make iso-stamp   apply overlay+bins to existing work/ then rebuild ISO"
	@echo "  make release     stack + install + initrd + receipt (+ iso-stamp if work ready)"
	@echo "  make receipt     write out/release-receipt.json"
	@echo "  make clean       remove built ELFs"

all stack wartime:
	$(MAKE) -C src all

install: all
	@bash scripts/install-stack.sh

initrd: all
	@bash boot/build-initrd.sh "$(ROOT)/out/initramfs.cpio.gz"

pack: initrd
	@bash boot/pack.sh

iso:
	@bash iso/build-all.sh

iso-stamp: install
	@bash iso/apply-stack.sh
	@bash iso/rebuild-iso.sh

receipt:
	@bash scripts/release-receipt.sh

release: install initrd receipt
	@if [ -d "$(ROOT)/work/edit/usr" ] || [ -d "$${SPEAR_WORK}/edit/usr" ] || [ -d /home/zachary/Desktop/SG/NewLatest/Spear/work/edit/usr ]; then \
	  echo "== work root present — iso-stamp =="; \
	  $(MAKE) iso-stamp; \
	else \
	  echo "== no work/edit yet — skip ISO rebuild =="; \
	  echo "   Link upstream work or: make iso  (full extract + remaster)"; \
	  echo "   Existing live ISO (if any): out/spear-latest.iso or SPEAR_ISO"; \
	fi
	@echo "RELEASE ready · version $(VERSION) · see out/release-receipt.json"

clean:
	$(MAKE) -C src clean
