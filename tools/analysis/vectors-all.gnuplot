# Multiplot layout showing the movements for each file,
# with the 6 sets for each method
set terminal png size 1024, 768
set output outfile

# Normalize both ranges to some screen resolution so that
# the graphs look roughly the same.
# Otherwise, the offset left of the graphs is normalized but
# not the one right of the graph and it looks like all the targets
# were on the right half ot the screen
set xrange [0:1920]
set yrange [1200:0]

# Use filename as title
set multiplot layout 3,4 columnsfirst title sprintf("%s", file)
set style data vector
set noxtics
set noytics
do for [method=0:2] {
	do for [set=0:5] {
		# only two out of three methods exist per file, so we need to check for existence
		file_exists = system(sprintf("[ ! -e %s-set-0-target-1-method-%d ]; echo $?", file, method))
		if (file_exists) {
			set xlabel sprintf("Set %i Method %i", set, method)
			plot for [target=1:15] sprintf('%s-set-%i-target-%i-method-%i', file, set, target, method) using 1:2:3:4 title ""
		}
	}
}
