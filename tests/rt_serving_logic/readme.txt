Regression Test Specifications

BINARY:
	Analytical serving logic unit-style test executable linked against AstraSim.

INPUTS:
	CONFIGS:
		In-memory JSON snippets covering explicit requests, generated traces, and invalid configurations.

OUTPUTS & REFERENCES:
		Process exit code and standard output validation. The binary uses assertions and fails fast on logic mismatches.
