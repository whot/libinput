# Show the movement vectors for each target data file
# call with gnuplot -e "file='$file'" vectors.gnuplot
set terminal png size 1024, 768
#set output sprintf("%s/%s.png", outpath, file)
#file=sprintf("%s/%s", path, file)
set output outfile
set style data vector
set xrange [0:*]
set yrange [1300:0]
set title file
plot file using 1:2:3:4 title ''

