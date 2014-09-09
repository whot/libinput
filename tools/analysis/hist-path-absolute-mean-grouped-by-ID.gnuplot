# print mean time to click for each ID group
set terminal png size 1920,1200
set output outfile

set xlabel "Index of Difficulty group"
set ylabel "Extra path in px"
set style data histogram
set style histogram cluster gap 1
set style fill solid 0.4 noborder

unset xtics
set xtics 0, 1

set yrange [0:*]
set grid y
plot file using 2:xtic(12) title 'smooth', \
	'' using 3 title 'stretched', \
	'' using 4 title 'linear'

