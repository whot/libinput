set terminal png size 1920,1200
set output outfile

set ylabel "Likert value"
set style data boxerrorbars
set style fill solid 0.4 noborder
set boxwidth 0.5

set noxtics
set xrange[-1:3]
set yrange[-2:2]

set grid y
plot file using ($0+0):2:9 title 'smooth vs. stretched', \
	'' using ($0+1):3:10 title 'smooth vs. linear', \
	'' using ($0+2):4:11 title 'stretched vs. linear'

