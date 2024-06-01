# Documentation

| Document | Contents |
|---|---|
| [SRS.md](SRS.md) | Software requirements specification SRS-HWVAL-F767: boot sequence, tasks, CLI, test framework, per-peripheral requirements, non-functional requirements and the traceability matrix. |
| [Hardware.md](Hardware.md) | Measured NUCLEO-F767ZI / STM32F767ZI board reference: pin map, clocks, memory and the values the tests compare against. |
| [TEST_CATALOGUE.md](TEST_CATALOGUE.md) | Every test case with its id, group, level, requirements and operator prompts. Generated, do not edit by hand. |

Regenerate the test catalogue after adding or changing a test:

```bash
python3 scripts/gen_testdoc.py
```

The catalogue is built from the test tables in `tests/` (single source of
truth, NFR-010). CI fails when it is stale.

Related documents live outside this directory:

- [../README.md](../README.md) — project overview, build and run
- [../docker/README.md](../docker/README.md) — build container, flashing, debugging
- [../protocol/README.md](../protocol/README.md) — HWLINK schema and code generation
- [../src/README.md](../src/README.md) — firmware source layout
