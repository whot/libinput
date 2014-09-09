# maps index of difficulty to time-to-click
#
set terminal png size 1920,1200
set output outfile

filter(a, b, x) = a == b ? x : -1000
set xrange[0:*]
# fixed maximum value to get the same scaling on all plot
set yrange[0:4500]
set xlabel "Index of Difficulty"
set ylabel "Time to click (ms)"
set style fill solid 0.2 noborder
set style line 1 linewidth 0.3
set style line 2 linewidth 0.3
set style line 3 linewidth 0.3

# need a multi-plot, it's too hard to see the three overlaid on top of each
# other
set multiplot layout 2,2
plot file using (filter(0, $2, $4)):5 title 'smooth' ls 1
plot '' using (filter(1, $2, $4)):5 title 'stretched' ls 2
plot '' using (filter(2, $2, $4)):5 title 'linear' ls 3

plot file using (filter(0, $2, $4)):5 title 'smooth' ls 1, \
   '' using (filter(1, $2, $4)):5 title 'stretched' ls 2, \
   '' using (filter(2, $2, $4)):5 title 'linear' ls 3
