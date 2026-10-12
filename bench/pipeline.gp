load ARG3
set terminal pngcairo size 1400,600 enhanced font "Sans,12"
set output ARG2
set datafile separator ','
set datafile missing NaN
stats ARG1 using 4 nooutput
min_capacity = STATS_min
max_capacity = STATS_max
set multiplot layout 1,words(PIPELINE_KERNEL_SIDES) title sprintf('Task 3: %d PNGs, %dx%d; median and min/max of %d runs (warm file cache)', PIPELINE_BATCH_SIZE, PIPELINE_IMAGE_SIDE, PIPELINE_IMAGE_SIDE, PIPELINE_RUNS)
set grid ytics
set key left top
set xlabel 'In-flight capacity'
set ylabel 'Batch time (ms)'
set xrange [min_capacity-0.25:max_capacity+0.25]
set xtics (int(word(PIPELINE_CAPACITIES, 1)))
do for [i=2:words(PIPELINE_CAPACITIES)] {
    set xtics add (int(word(PIPELINE_CAPACITIES, i)))
}
set yrange [0:*]
do for [k in PIPELINE_KERNEL_SIDES] {
    kernel = int(k)
    set title sprintf('%dx%d kernel: %s', kernel, kernel, kernel == 1 ? 'I/O and codec workload' : 'added convolution work')
    stats ARG1 using ($1==kernel && strcol(2) eq 'serial' ? $5 : 1/0) nooutput
    baseline = STATS_mean
    plot baseline with lines dt 2 lc rgb '#888888' title 'sequential median', for [w in PIPELINE_WORKERS] ARG1 using ($1==kernel && strcol(2) eq 'pipeline' && $3==int(w) ? $4 : NaN):5:6:7 with yerrorlines lw 2 title w.' requested workers'
}
unset multiplot
