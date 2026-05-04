Regression Tests Guideline

Before performing regression tests, please make sure the compilation is successful. 

To perform individual regression test, run:
	./rt_xxx/run.sh

To perform all regression tests, run:
	./run_all.sh

Current regression groups:
	- rt_template: baseline template/example harness
	- rt_serving_logic: serving logic unit-style checks
	- rt_serving_baseline: event-driven Option 2 serving regressions
	- rt_analytical: TP/PP crossover and attention/FFN disaggregation regressions

To add new regression test, 
	1. Create new folder named rt_xxx.
	2. Follow rt_template by providing inputs, references, run script, and readme.txt of test specifications. 
	3. Edit ./run_all.sh script to include ./rt_xxx/run.sh script. 

Environment note:
		The rt_template workload generator now exports the local Chakra Python path before running.
		If you still see Python import issues, verify that your environment has the required Python dependencies installed for Chakra and protobuf generation.
