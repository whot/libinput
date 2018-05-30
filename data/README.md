= libinput data file format =

This directory contains hardware quirks used by libinput to work around bugs
in the hardware, device behavior and to supply information not obtained
through the kernel device.

**THIS IS NOT STABLE API**

The data format may change at any time. If your data file is not part of the
libinput git tree, do not expect it to work after an update. Absolutely no
guarantees are made for backwards-compatibility.

**THIS IS NOT A CONFIGURATION API**

Use the `libinput_device_config_foo()` functions for device configuration.
The quirks are hardware quirks only.

== Data file naming ==

Data files are read in versionsort order, read order determines how values
override each other. A values read later override previously values. The
current structure is 10-generic-foo.quirks for generic settings,
30-vendor-foo.quirks for vendor-specific settings and 50-system-foo.quirks
for system vendors. This is not a fixed naming scheme and may change at any
time. It's an approximation only because some vendors are also system
vendors, e.g. Microsoft makes devices and laptops.

Laptop-specific quirks should always go into the laptop vendor's file.

== Sections, matches and values ==

A data file must contain at least one section, each section must have at
least one `Match` tag and at least one of either `Attr` or `Model`. Section
names are free-form and may contain spaces.

```
# This is a comment
[Some touchpad]
MatchBus=usb
# No quotes around strings
MatchName=*Synaptics Touchpad*
AttrSizeHint=50x50
ModelSynapticsTouchpad=1

[Apple touchpad]
MatchVendor=0x5AC
MatchProduct=0x123
ModelAppleTouchpad=1
```

Comments are lines starting with `#`.

All `Model` tags take a value of either `1` or `0`.

All `Attr` tag values are specific to that attribute.

== Parser errors ==

The following will cause parser errors and are considered invalid data
files:

* Whitespace at the beginning of the line
* Sections without at least one `Match*` entry
* Sections with the same `Match*` entry repeated
* Sections without at least one of `Model*` or `Attr` entries
* A `Model` tag with a value other than `1` or `0`
* A string property with enclosing quotes

== Debugging ==

When modifying a data file, use the `libinput list-quirks` tool to
verify the changes. The tool can be pointed at the data directory to
analyse, use `--verbose` to get more info. For example:

```
libinput list-quirks --data-dir /path/to/git/repo/data/ --verbose /dev/input/event0
```
