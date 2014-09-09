# Questionnaire analysis
#
# Data file: questionnaire-1.dat questionnaire-2.dat questionnaire-3.dat
# Data format:
# qidx qname likert1-count l2-count ... mean stddev

set terminal png size 1920,1200
set output outfile
set title "Questionnaire answer distribution"
set style data histogram
#set key outside below center horizontal
set key under nobox
set style histogram rowstacked gap 1
set boxwidth .5 absolute
set style fill solid 0.4 noborder
set xlabel ' ' offset 0, -1
set ylabel 'Frequency'
set yrange [0:*]
unset xtics
set xtics nomirror rotate by -45 scale 0
set grid y

plot newhistogram 'smooth' lt 1, \
file using 3 title 'strongly disagree',  \
     '' using 4 title 'disagree', \
     '' using 5 title 'neutral', \
     '' using 6 title 'agree', \
     '' using 7:xtic(2) title 'strongly agree', \
newhistogram 'stretched' lt 1, \
'' using 8 notitle,  \
     '' using 9 notitle, \
     '' using 10 notitle, \
     '' using 11 notitle, \
     '' using 12:xtic(2) notitle, \
newhistogram 'linear' lt 1, \
'' using 13 notitle,  \
     '' using 14 notitle, \
     '' using 15 notitle, \
     '' using 16 notitle, \
     '' using 17:xtic(2) notitle
