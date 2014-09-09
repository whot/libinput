# print the index of difficulty for each target

set terminal png size 1920,1200
set output outfile

filter(a, b, x) = a == b ? x : -1000
set xrange[0:*]
unset xtics
set ytics 0, 1
set ylabel "index of difficulty"
set style fill solid 0.2 noborder
set style line 1 linewidth 0.3
set style line 2 linewidth 0.3
set style line 3 linewidth 0.3

set style line 4 linewidth 0.4 linecolor rgb "black"

plot file using (filter(0, $2, $0)):4 title 'smooth' ls 1, \
   '' using (filter(1, $2, $0)):4 title 'stretched' ls 2, \
   '' using (filter(2, $2, $0)):4 title 'linear' ls 3, \
   16.8 ls 4 notitle, 25 ls 4 notitle, 8.4 ls 4 notitle, 12.9 ls 4 notitle, 4.2 ls 4 notitle

