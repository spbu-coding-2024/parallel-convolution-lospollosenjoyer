load ARG3
set terminal pngcairo size 1400,600 enhanced font "Sans,12"
set output ARG2
set datafile separator ','
set datafile missing NaN
set multiplot layout 1,2 title sprintf('Sequential direct correlation; median of %d runs (bars: min/max)', SEQUENTIAL_RUNS)
set grid ytics
set key left top
set xlabel 'Image pixels (millions)'
set ylabel 'Correlation time (ms)'
set title 'Time vs image size'
plot for [k in SEQUENTIAL_KERNEL_SIDES] ARG1 using ($3 == int(k) ? $1*$2/1e6 : NaN):4:5:6 with yerrorlines lw 2 pt 7 title k.'x'.k
set xlabel 'Kernel area (coefficients)'
set title 'Time vs kernel size'
plot for [n in SEQUENTIAL_IMAGE_SIDES] ARG1 using ($1 == int(n) ? $3*$3 : NaN):4:5:6 with yerrorlines lw 2 pt 7 title n.'x'.n
unset multiplot
