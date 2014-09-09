set terminal png size 1920,1200
set output outfile
set title "Questionnaire answers mean"
set style data histogram
set style histogram gap 1
set boxwidth .9 absolute
set style fill solid 0.3
set yrange [-2:2]
unset ytics 
set grid y
set ytics -2, 1, 2
set ytics add ("neutral" 0)
set ytics add ("agree" 1)
set ytics add ("strongly agree" 2)
set ytics add ("disagree" -1)
set ytics add ("strongly disagree" -2)
set xtics nomirror rotate by -45 scale 0

plot file using 2:xtic(12) title 'smooth',  \
'' using 3:xtic(12) title 'stretched',  \
'' using 4:xtic(12) title 'linear'

