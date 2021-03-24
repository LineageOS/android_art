# ART Metrics

This directory contains most of ART's metrics framework. Some portions that
rely on the runtime can be found in the `runtime/metrics` directory.

## Declaring Metrics

ART's internal metrics are listed in the `ART_METRICS` macro in `metrics.h`.
Each metric has a `METRIC` entry which takes a name for the metric, a type
 (such as counter or histogram), and any additional arguments that are needed.

### Counters

    METRIC(MyCounter, MetricsCounter)

Counters store a single value that can be added to. This is useful for counting
events, counting the total amount of time spent in a section of code, and other
uses.

### Accumulators

    METRIC(MyAccumulator, MetricsAccumulator, type, accumulator_function)

Example:

    METRIC(MaximumTestMetric, MetricsAccumulator, int64_t, std::max<int64_t>)

Accumulators are a generalization of counters that takes an accumulator
function that is used to combine the new value with the old value. Common
choices are the min and max function. To be valid, the accumulator function
must be monotonic in its first argument. That is, if
`x_new == accumulator_function(x_old, y)` then `x_new ⪯ x_old` for some
ordering relation `⪯` (e.g. less-than-or-equal or greater-than-or-equal).

### Histograms

    METRIC(MyHistogram, MetricsHistogram, num_buckets, minimum_value, maximum_value)

Histograms divide a range into several buckets and count how many times a value
falls within each bucket. They are useful for seeing the overall distribution
for different events.

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
