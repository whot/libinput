#!/bin/bash

tool=`dirname $0`/ptraccel-debug
gnuplot=/usr/bin/gnuplot

outfile="ptraccel-linear"
for speed in -1 -0.75 -0.5 -0.25 0 0.5 1; do
	$tool --mode=accel --dpi=1000 --filter=linear --speed=$speed > $outfile-$speed.gnuplot
done
$gnuplot <<EOF
set terminal svg enhanced background rgb 'white'
set output "$outfile.svg"
set xlabel "speed in units/us"
set ylabel "accel factor"
set style data lines
set yrange [0:3]
set xrange [0:0.003]
plot "$outfile--1.gnuplot" using 1:2 title "-1.0", \
	"$outfile--0.75.gnuplot" using 1:2 title "-0.75", \
	"$outfile--0.5.gnuplot" using 1:2 title "-0.5", \
	"$outfile--0.25.gnuplot" using 1:2 title "-0.25", \
	"$outfile-0.gnuplot" using 1:2 title "0.0", \
	"$outfile-0.5.gnuplot" using 1:2 title "0.5", \
	"$outfile-1.gnuplot" using 1:2 title "1.0"
EOF

outfile="ptraccel-low-dpi"
for dpi in 200 400 800 1000; do
	$tool --mode=accel --dpi=$dpi --filter=low-dpi > $outfile-$dpi.gnuplot
done

$gnuplot <<EOF
set terminal svg enhanced background rgb 'white'
set output "$outfile.svg"
set xlabel "speed in units/us"
set ylabel "accel factor"
set style data lines
set yrange [0:5]
set xrange [0:0.003]
plot "$outfile-200.gnuplot" using 1:2 title "200dpi", \
     "$outfile-400.gnuplot" using 1:2 title "400dpi", \
     "$outfile-800.gnuplot" using 1:2 title "800dpi", \
     "$outfile-1000.gnuplot" using 1:2 title "1000dpi"
EOF

outfile="ptraccel-touchpad"
$gnuplot <<EOF
set terminal svg enhanced background rgb 'white'
set output "$outfile.svg"
set xlabel "speed in mm/s"
set ylabel "accel factor"
set style data lines
set xrange [0:400]
plot \
$(
	for speed in -1 -0.5 -0.2 0 0.2 0.5 1; do
		$tool --mode=accel --dpi=1000 --filter=touchpad --speed=$speed> $outfile-$speed.gnuplot
		echo "\"$outfile-$speed.gnuplot\" using 1:2 title '$speed', \\"
	done
)
EOF

outfile="ptraccel-trackpoint"
$tool --mode=accel --dpi=1000 --filter=linear > $outfile-mouse.gnuplot
for constaccel in 1 2 3; do
	dpi=$((1000/$constaccel))
	$tool --mode=accel --dpi=$dpi --filter=trackpoint > $outfile-trackpoint-$constaccel.gnuplot
done
$gnuplot <<EOF
set terminal svg enhanced background rgb 'white'
set output "$outfile.svg"
set xlabel "speed in units/us"
set ylabel "accel factor"
set style data lines
set yrange [0:5]
set xrange [0:0.003]
plot "$outfile-mouse.gnuplot" using 1:2 title "linear (mouse)", \
     "$outfile-trackpoint-1.gnuplot" using 1:2 title "const accel 1", \
     "$outfile-trackpoint-2.gnuplot" using 1:2 title "const accel 2", \
     "$outfile-trackpoint-3.gnuplot" using 1:2 title "const accel 3"
EOF
