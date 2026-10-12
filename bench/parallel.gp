load ARG3
set terminal pngcairo size 1400,600 enhanced font "Sans,12"
set output ARG2
set datafile separator ','
set datafile missing NaN
stats ARG1 using 1 nooutput
image_side = STATS_max
stats ARG1 using 4 nooutput
min_threads = STATS_min
max_threads = STATS_max
set multiplot layout 1,words(PARALLEL_KERNEL_SIDES) title sprintf('Task 2: %dx%d, paired speedup: median and min/max of %d runs; grid %dx%d', image_side, image_side, PARALLEL_RUNS, PARALLEL_GRID_COLUMNS, PARALLEL_GRID_ROWS)
set grid ytics
set xlabel 'Worker threads'
set ylabel 'Sequential / parallel time (same run)'
set yrange [0:*]
set bars 0.8
set xrange [min_threads-0.25:max_threads+0.25]
set xtics min_threads,1,max_threads
set key left top
do for [k in PARALLEL_KERNEL_SIDES] {
    kernel = int(k)
    set title sprintf('%dx%d kernel', kernel, kernel)
    plot for [m in PARALLEL_MODES] ARG1 using ($1==image_side && $2==kernel && strcol(3) eq m ? $4 : NaN):8:9:10 with yerrorlines lw 2 title m
}
unset multiplot
