Regression Test Specifications

BINARY:
	Analytical backend with synthetic request serving mode enabled.

INPUTS:
		REQUESTS:
			Explicit synthetic request configurations plus generated Poisson traces for deterministic load and prompt-length checks.
	SYSTEM:
		4-NPU ring system using native collective implementations.
	NETWORK:
		4-NPU analytical ring network.
	MEMORY:
		No remote memory expansion.

OUTPUTS & REFERENCES:
		Standard output and CSV comparison for deterministic explicit scenarios, JSON/property checks for generated traces, and metadata validation for structured outputs.
