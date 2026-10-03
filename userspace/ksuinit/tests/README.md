# ksuinit regression tests

These tests exercise the production ELF parser, kallsyms normalization and the
vermagic retry helper without mounting filesystems or loading a kernel module.
They include malformed/truncated ELF metadata, overflowed offsets, overlapping
tables, hidden addresses and 10,000 deterministic input mutations.

Run on a Linux host from the repository root:

```sh
python3 userspace/ksuinit/tests/run_host_tests.py --cxx clang++ --sanitize
```

On Windows, pass `--zig /path/to/zig.exe` to use Zig's native C++ toolchain and
standalone musl `elf.h`. No Android system calls are used.
The handoff test executes the real entry point with mocked process/exec calls and
checks successful restoration, fallback paths, argument preservation and refusal
to initialize outside PID 1.
The logging test covers truncation, interrupted writes and preserving the caller's
`errno` after both successful and failed writes.
An Android arm64 build and device boot tests are still required to validate the
actual PID 1 handoff, procfs ownership, LKM insertion and recovery paths.
