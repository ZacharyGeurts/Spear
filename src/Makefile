# SPDX-License-Identifier: MIT
# Spear — C++ and lower only. Scripts FORBIDDEN as product path.
CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 -I.

CORE_SRCS = spear_main.cpp spear_ffat.cpp spear_elevate.cpp spear_field1.cpp spear_chip.cpp spear_fieldram.cpp spear_fieldmem.cpp spear_patdict.cpp
CORE_HDRS = spear_ffat.hpp spear_elevate.hpp spear_field1.hpp spear_chip.hpp spear_fieldram.hpp spear_fieldmem.hpp spear_patdict.hpp spear_sha256.hpp spear_common.hpp

.PHONY: all clean install wartime field

FIELD_TOOLS = field-nvtop fieldbox

all: spear spear-hard-dispose spear-copilot-monitor spear-wartime spear-kill-copilot spear-www spear-planet spear-export spear-fleet-link $(FIELD_TOOLS)

spear: $(CORE_SRCS) $(CORE_HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(CORE_SRCS) -lm

spear-hard-dispose: spear_hard_dispose.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_hard_dispose.cpp

spear-copilot-monitor: spear_copilot_monitor.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_copilot_monitor.cpp

spear-wartime: spear_wartime.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_wartime.cpp

spear-kill-copilot: spear_kill_copilot.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_kill_copilot.cpp

spear-www: spear_www.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_www.cpp

spear-planet: spear_planet.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_planet.cpp

spear-export: spear_export.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_export.cpp

spear-fleet-link: spear_fleet_link.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ spear_fleet_link.cpp -lpthread

field-nvtop: field_nvtop.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ field_nvtop.cpp -lpthread

fieldbox: field_box.cpp spear_common.hpp
	$(CXX) $(CXXFLAGS) -o $@ field_box.cpp

wartime: spear-wartime spear-kill-copilot spear-hard-dispose spear-www spear-planet spear-export spear-fleet-link
field-all: all

install: all
	install -m 0755 spear-hard-dispose spear-copilot-monitor spear-wartime spear-kill-copilot spear-www spear-planet spear-export spear-fleet-link field-nvtop fieldbox \
	  $(HOME)/.local/bin/ 2>/dev/null || true
	install -m 0755 spear-hard-dispose spear-copilot-monitor spear-wartime spear-kill-copilot spear-www spear-planet spear-export spear-fleet-link field-nvtop fieldbox \
	  /home/zachary/Desktop/SG/Spear/overlay/usr/local/bin/ 2>/dev/null || true
	-install -m 0755 spear /usr/local/bin/spear 2>/dev/null || true
	ln -sfn $(HOME)/.local/bin/spear-planet $(HOME)/.local/bin/spear-planet-live 2>/dev/null || true
	ln -sfn $(HOME)/.local/bin/spear-fleet-link $(HOME)/.local/bin/spear-rack-guard 2>/dev/null || true
	ln -sfn $(HOME)/.local/bin/field-nvtop $(HOME)/.local/bin/nv-top 2>/dev/null || true
	ln -sfn $(HOME)/.local/bin/field-nvtop $(HOME)/.local/bin/nvtop 2>/dev/null || true
	ln -sfn $(HOME)/.local/bin/field-nvtop $(HOME)/.local/bin/field-gpu 2>/dev/null || true
	ln -sfn $(HOME)/.local/bin/fieldbox $(HOME)/.local/bin/field 2>/dev/null || true
	# Own core names: top, ls, ps, …
	for a in top ls ps df free cat uname id whoami hostname env clear pwd echo head wc which \
	         mkdir rm sleep kill stat uptime true false; do \
	  ln -sfn fieldbox $(HOME)/.local/bin/$$a 2>/dev/null || true; \
	done

clean:
	rm -f spear spear-hard-dispose spear-copilot-monitor spear-wartime spear-kill-copilot spear-www spear-planet spear-export spear-fleet-link field-nvtop fieldbox
