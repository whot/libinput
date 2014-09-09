# print number of targets for each ID group

set terminal png size 1920,1200
set output outfile

set xlabel "Index of Difficulty group"
set ylabel "Number of targets"
set style data histogram
set style histogram cluster gap 1
set style fill solid 0.4 noborder

set yrange[0:*]

unset xtics
set xtics 0, 1

set grid y
plot file using 2:xtic(5) title 'smooth', \
	'' using 3 title 'stretched', \
	'' using 4 title 'linear'

