#!/bin/sh
#
# This does not honor builddir properly, because we cannot get to it
# here. Does anyone build to something but prefix/bin?
#
bindir="${DESTDIR}/${MESON_INSTALL_PREFIX}/bin"
mkdir -p "$bindir"
cp "${MESON_SOURCE_ROOT}/tools/libinput-list-devices.compat" "${bindir}/libinput-list-devices"
cp "${MESON_SOURCE_ROOT}/tools/libinput-debug-events.compat" "${bindir}/libinput-debug-events"
