set terminal png size 1920,1200
set output outfile

off(x, off) = off * 6 + x

set title "Scroll method preferences"
set xlabel "Likert value"
set ylabel "Frequency"
set style data boxes
set style histogram cluster gap 1
set boxwidth 0.9
set style fill solid 0.4 noborder
set yrange[0:9]

unset xtics
set xtics nomirror rotate by -45 scale 0 -1 50 100
set xrange[-4:35]
do for [xoff=0:5] {
	set xtics add ("strongly disagree" (off(-2, xoff)))
	set xtics add ("disagree" (off(-1, xoff)))
	set xtics add ("neutral" (off(0, xoff)))
	set xtics add ("agree" (off(1, xoff)))
	set xtics add ("strongly agree" (off(2, xoff)))

}


plot file using (off($3, 0)):5 title 'smooth over stretched', \
   '' using (off($3, 1)):13 title 'smooth over linear', \
   '' using (off($3, 2)):9 title 'stretched over linear', \
   '' using (off($3, 3)):7 title 'stretched over smooth', \
   '' using (off($3, 4)):15 title 'linear over smooth', \
   '' using (off($3, 5)):11 title 'linear over stretched'

