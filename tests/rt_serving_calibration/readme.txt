Tests the serving calibration helper against fixed anchors with exact expected
latency terms, ensuring recovered cost-model parameters stay within a tight
error bound. Also tests aggregate calibration-root fitting across colocated and
PD-disaggregated runs, including skipped incomplete-run reporting and proxy
transfer fitting. The aggregate fixture covers both scheduler-trace stage
fitting and request-metrics latency fitting.
