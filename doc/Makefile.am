EXTRA_DIST = \
	     middle-button-emulation.svg \
	     touchpad-tap-state-machine.svg \
	     touchpad-softbutton-state-machine.svg

if BUILD_DOCS

noinst_DATA = html/index.html

header_files = \
	$(top_srcdir)/src/libinput.h \
	$(top_srcdir)/README.txt \
	$(srcdir)/absolute-axes.dox \
	$(srcdir)/clickpad-softbuttons.dox \
	$(srcdir)/device-configuration-via-udev.dox \
	$(srcdir)/faqs.dox \
	$(srcdir)/gestures.dox \
	$(srcdir)/normalization-of-relative-motion.dox \
	$(srcdir)/palm-detection.dox \
	$(srcdir)/page-hierarchy.dox \
	$(srcdir)/scrolling.dox \
	$(srcdir)/seats.dox \
	$(srcdir)/t440-support.dox \
	$(srcdir)/tapping.dox \
	$(srcdir)/test-suite.dox \
	$(srcdir)/tools.dox

diagram_files = \
	$(srcdir)/dot/seats-sketch.gv \
	$(srcdir)/dot/seats-sketch-libinput.gv \
	$(srcdir)/dot/libinput-stack-wayland.gv \
	$(srcdir)/dot/libinput-stack-xorg.gv \
	$(srcdir)/dot/libinput-stack-gnome.gv \
	$(srcdir)/svg/software-buttons.svg \
	$(srcdir)/svg/clickfinger.svg \
	$(srcdir)/svg/button-scrolling.svg \
	$(srcdir)/svg/edge-scrolling.svg \
	$(srcdir)/svg/palm-detection.svg \
	$(srcdir)/svg/pinch-gestures.svg \
	$(srcdir)/svg/swipe-gestures.svg \
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

html/index.html: libinput.doxygen $(header_files) $(diagram_files) $(style_files)
	$(AM_V_GEN)(cat $<; \
          echo "INPUT = $(header_files)"; \
          ) | $(DOXYGEN) -

clean-local:
	$(AM_V_at)rm -rf html

doc_src= $(shell find html -type f -printf "html/%P\n" 2>/dev/null)
EXTRA_DIST += $(builddir)/html/index.html \
	      $(doc_src) \
	      $(diagram_files) \
	      $(header_files) \
	      $(style_files)
endif

# make sure doc was built before running dist
dist-hook:
	@test -f $(distdir)/html/index.html || (\
		echo "******************************************************" && \
		echo "Couldn't find documentation files, refusing make dist." && \
		echo "Install doxygen to build documentation for tarball." && \
		echo "******************************************************" && \
		test )
