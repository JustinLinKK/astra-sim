Regression Test Specifications

BINARY:
	Describe the binary under test. e.g., analytical with congestion awareness. 
INPUTS: 
	WORKLOAD: 
		Describe the workload under test. e.g., single all reduce communication node. 
	SYSTEM: 
		Describe the system configuration under test. e.g., all reduce through ring. 
	NETWORK: 
		Describe the network configuration under test. e.g., single dimensional ring of 8 NPUs. 
	MEMORY: 
		Describe the memory configuration under test. e.g., no remote memory expansion. 
OUTPUTS & REFERENCES: 
	Describe the validation method. e.g., standard output comparison. 

NOTE:
	Generating the Chakra ET inputs requires the Chakra Python package path to be visible to python3.
	In this environment, the template generator currently fails with ModuleNotFoundError: chakra unless PYTHONPATH is configured.
