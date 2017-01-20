EXTRA_DIST = \
	     middle-button-emulation.svg \
	     touchpad-tap-state-machine.svg \
	     touchpad-softbutton-state-machine.svg

header_files = \
	$(top_srcdir)/src/libinput.h \
	$(top_srcdir)/README.txt \
	$(srcdir)/absolute-axes.dox \
	$(srcdir)/absolute-coordinate-ranges.dox \
	$(srcdir)/building.dox \
	$(srcdir)/clickpad-softbuttons.dox \
	$(srcdir)/contributing.dox \
	$(srcdir)/device-configuration-via-udev.dox \
	$(srcdir)/faqs.dox \
	$(srcdir)/gestures.dox \
	$(srcdir)/middle-button-emulation.dox \
	$(srcdir)/normalization-of-relative-motion.dox \
	$(srcdir)/palm-detection.dox \
	$(srcdir)/page-hierarchy.dox \
	$(srcdir)/pointer-acceleration.dox \
	$(srcdir)/reporting-bugs.dox \
	$(srcdir)/scrolling.dox \
	$(srcdir)/seats.dox \
	$(srcdir)/switches.dox \
	$(srcdir)/t440-support.dox \
	$(srcdir)/tablet-support.dox \
	$(srcdir)/tapping.dox \
	$(srcdir)/test-suite.dox \
	$(srcdir)/tools.dox \
	$(srcdir)/touchpad-jumping-cursors.dox \
	$(srcdir)/touchpads.dox

diagram_files = \
	$(srcdir)/dot/seats-sketch.gv \
	$(srcdir)/dot/seats-sketch-libinput.gv \
	$(srcdir)/dot/libinput-stack-wayland.gv \
	$(srcdir)/dot/libinput-stack-xorg.gv \
	$(srcdir)/dot/libinput-stack-gnome.gv \
	$(srcdir)/dot/evemu.gv \
	$(srcdir)/svg/button-scrolling.svg \
	$(srcdir)/svg/clickfinger.svg \
	$(srcdir)/svg/clickfinger-distance.svg \
	$(srcdir)/svg/edge-scrolling.svg \
	$(srcdir)/svg/gesture-2fg-ambiguity.svg \
	$(srcdir)/svg/palm-detection.svg \
	$(srcdir)/svg/pinch-gestures.svg \
	$(srcdir)/svg/pinch-gestures-softbuttons.svg \
	$(srcdir)/svg/ptraccel-linear.svg \
	$(srcdir)/svg/ptraccel-low-dpi.svg \
	$(srcdir)/svg/ptraccel-touchpad.svg \
	$(srcdir)/svg/ptraccel-trackpoint.svg \
	$(srcdir)/svg/software-buttons.svg \
	$(srcdir)/svg/swipe-gestures.svg \
	$(srcdir)/svg/tablet-axes.svg \
	$(srcdir)/svg/tablet-cintiq24hd-modes.svg \
	$(srcdir)/svg/tablet-interfaces.svg \
	$(srcdir)/svg/tablet-intuos-modes.svg \
	$(srcdir)/svg/tablet-left-handed.svg \
	$(srcdir)/svg/tablet-out-of-bounds.svg \
	$(srcdir)/svg/tablet.svg \
	$(srcdir)/svg/tap-n-drag.svg \
	$(srcdir)/svg/thumb-detection.svg \
	$(srcdir)/svg/top-software-buttons.svg \
	$(srcdir)/svg/touchscreen-gestures.svg \
	$(srcdir)/svg/twofinger-scrolling.svg

style_files = \
	      style/header.html \
	      style/footer.html \
	      style/customdoxygen.css \
	      style/bootstrap.css

if BUILD_DOCS

noinst_DATA = html/index.html

html/index.html: libinput.doxygen $(header_files) $(diagram_files) $(style_files)
	$(AM_V_GEN)(cat $<; \
          echo "INPUT = $(header_files)"; \
          ) | $(DOXYGEN) -

clean-local:
	$(AM_V_at)rm -rf html

doc_src= $(shell find html -type f -printf "html/%P\n" 2>/dev/null)
EXTRA_DIST += $(builddir)/html/index.html \
	      $(doc_src)
endif

EXTRA_DIST += $(diagram_files) \
	      $(header_files) \
	      $(style_files)

local_svg = $(shell find $(srcdir)/svg -type f -printf "$(srcdir)/svg/%P\n" 2>/dev/null)
local_dotfiles = $(shell find $(srcdir)/dot -type f -printf "$(srcdir)/dot/%P\n" 2>/dev/null)

check:
	@files=`echo $(local_svg) $(local_dotfiles) $(diagram_files) | tr ' ' '\n' | sort | uniq -u` && \
		test -z "$$files" || (\
		echo "SVG/dotfiles files present but missing from the Makefile.am:" && \
		echo "$$files" && \
		test)

# make sure doc was built before running dist
dist-hook:
	@test -f $(distdir)/html/index.html || (\
		echo "******************************************************" && \
		echo "Couldn't find documentation files, refusing make dist." && \
		echo "Install doxygen to build documentation for the tarball." && \
		echo "******************************************************" && \
		test )
