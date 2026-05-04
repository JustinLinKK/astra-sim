# ASTRA-sim
[ASTRA-sim](https://astra-sim.github.io/) is a distributed AI system simulator. It models the end-to-end software and hardware stack of modern AI systems - encompassing workload scheduling, collective communication algorithms, and hardware architectures (compute/memory/network). Through a suite of APIs, it enables plug-and-play of external open/proprietary components for modeling different parts of the AI system. This provides end-to-end multi-fidelity simulation capabilities for aiding in design and deployment of next-generation distributed AI systems. 

## Analytical Modes
This repository includes a unified analytical layer for three inference studies:

1. `tp_pp_crossover`
   Sweeps TP versus PP tradeoffs for 70B-class dense models.
2. `serving_disagg_colocated`
   Runs the existing disaggregated-versus-colocated serving simulator through the analytical frontend without changing TTFT, TPOT, E2E, queueing, or goodput behavior.
3. `attention_ffn_disaggregation`
   Compares homogeneous GPU against heterogeneous GPU+LPU placement for trillion-parameter MoE decode.

The shared implementation lives under [astra-sim/analytical](</home/justin/astra-sim/astra-sim/analytical>) and keeps model specs, hardware specs, interconnects, cost models, and result writers in one framework-native module.

## Quick Start
Build the analytical binaries with:

```bash
./build/astra_analytical/build.sh
```

The main binaries are:

- `build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware`
- `build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Unaware`

Run the shipped analytical configs with:

```bash
./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
  --analytical-config=$(realpath configs/tp_pp_70b.yaml)

./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
  --analytical-config=$(realpath configs/serving_disagg_colocated.yaml)

./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
  --analytical-config=$(realpath configs/attention_ffn_gpu_lpu_moe.yaml)
```

Important behavior:

- `--analytical-config` is the public entrypoint for these analytical modes.
- `tp_pp_crossover` and `attention_ffn_disaggregation` are closed-form analytical runs and do not instantiate the event-driven serving runtime.
- `serving_disagg_colocated` still uses the existing serving simulator underneath, so serving behavior stays on the established path.

## Analytical Configs
The checked-in examples live in [configs](</home/justin/astra-sim/configs>):

- [configs/tp_pp_70b.yaml](/home/justin/astra-sim/configs/tp_pp_70b.yaml)
- [configs/serving_disagg_colocated.yaml](/home/justin/astra-sim/configs/serving_disagg_colocated.yaml)
- [configs/attention_ffn_gpu_lpu_moe.yaml](/home/justin/astra-sim/configs/attention_ffn_gpu_lpu_moe.yaml)

These YAMLs are fully explicit. Output paths are resolved relative to the YAML file location, which makes it easy to copy a config and redirect outputs without rewriting absolute paths.

## Outputs
`tp_pp_crossover` writes a row-oriented CSV plus a summary JSON containing pure-TP versus pure-PP crossover records.

`serving_disagg_colocated` keeps the existing request metrics CSV, summary JSON, and metadata JSON format from the serving simulator.

`attention_ffn_disaggregation` writes a row-oriented CSV plus a summary JSON comparing:

- `homogeneous_gpu`
- `heterogeneous_gpu_lpu`

## Tests
Run the full regression suite with:

```bash
./tests/run_all.sh
```

Run the analytical regression directly with:

```bash
./tests/rt_analytical/run.sh
```

Run the analytical logic tests directly with:

```bash
./build/astra_analytical/build/bin/AstraSim_Analytical_Logic_Tests
```

## Documentation
For a focused usage guide covering config structure, commands, outputs, and extension points, see [docs/project/analytical-guide.md](/home/justin/astra-sim/docs/project/analytical-guide.md).


### Overview and Documentation
Here is a concise visual summary of ASTRA-sim, showing its layers and APIs:
![alt text](https://github.com/astra-sim/astra-sim/blob/master/docs/images/astrasim_overview_codesign.png)

For a comprehensive understanding of the tool, and to gain insights into its capabilities, please visit our [website](https://astra-sim.github.io/).

For information on how to use ASTRA-sim, please visit our [Wiki](https://astra-sim.github.io/astra-sim-docs/index.html).

ASTRA-sim accepts MLCommons Chakra Execution Traces as workload-layer inputs. For details, please visit [Chakra Github](https://github.com/mlcommons/chakra).


### Releases and Contributions

ASTRA-sim is currently at **version 2.0.**
The previous version, ASTRA-sim 1.0, is available in the `ASTRA-sim-1.0` [branch](https://github.com/astra-sim/astra-sim/tree/ASTRA-sim-1.0).

We encourage community contributions to ASTRA-sim via PRs.


## Contact Us
For any questions about using ASTRA-sim, you can email the ASTRA-sim User Mailing List: astrasim-users@googlegroups.com

To join the mailing list, please fill out the following form: https://forms.gle/18KVS99SG3k9CGXm6


We appreciate your interest and support in ASTRA-sim!
