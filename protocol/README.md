# HWLINK protocol schema

`hwlink.fbs` is the FlatBuffers schema shared by every SOCcentric STM32
firmware and by the host tools (HWVAL Studio, HWVAL Factory, the bootloader
tools). The normative protocol description is SRS-GUI-F767 Appendix A.

The schema is append-only within a major version: fields may be added at the
end and the minor version raised, but nothing is removed or reordered
(LNK-013).

## Generated code

The flatcc C headers derived from this schema are committed in
`../src/generated/` so the build container does not need a host `flatcc`
compiler:

```
src/generated/flatbuffers_common_reader.h
src/generated/flatbuffers_common_builder.h
src/generated/hwlink_reader.h
src/generated/hwlink_builder.h
src/generated/hwlink_verifier.h
```

When `flatcc` is on `PATH`, `CMakeLists.txt` regenerates them into
`build/generated/` and the build uses that copy. Regenerate and commit the
files after editing the schema:

```bash
flatcc -a -o src/generated protocol/hwlink.fbs
```

## Runtime

The released flatcc 0.6.1 runtime is vendored in `../third_party/flatcc`; only
builder, verifier and emitter are linked. All allocation goes through the
static arena hooks in `../src/protocol/flatcc_port.c` (no `malloc`), sized by
the `HWVAL_FLATCC_HEAP_KB` CMake cache variable.
