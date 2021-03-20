# ART Metrics

This directory contains most of ART's metrics framework. Some portions that
rely on the runtime can be found in the `runtime/metrics` directory.

## Declaring Metrics

ART's internal metrics are listed in the `ART_METRICS` macro in `metrics.h`.
Each metric has a `METRIC` entry which takes a name for the metric, a type
 (such as counter or histogram), and any additional arguments that are needed.

### Counters

    METRIC(MyCounter, MetricsCounter)

### Histograms

    METRIC(MyHistogram, MetricsHistogram, num_buckets, minimum_value, maximum_value)

The `num_buckets` parameter affects memory usage for the histogram and data
usage for exported metrics. It is recommended to keep this below 16. The
`minimum_value` and `maximum_value` parameters are needed because we need to
know what range the fixed number of buckets cover. We could keep track of the
observed ranges and try to rescale the buckets or allocate new buckets, but
this would make incrementing them more expensive than just some index
arithmetic and an add. Values outside the range get clamped to the nearest
bucket (basically, the two buckets on either side are infinitely long). If we
see those buckets being way taller than the others, it means we should consider
expanding the range.
