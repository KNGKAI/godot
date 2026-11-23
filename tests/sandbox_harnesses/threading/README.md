# Gate 2 worker and sanitizer audit

The Gate 2 doctest drives the production `SimulationServer` for 10,000 ticks. Each of eight equal-priority modules writes to its own preallocated scalar channel on every tick. Their first callbacks wait up to two seconds for the first wave of workers to arrive. This makes overlap observable even when a module's normal tick finishes in microseconds. Each callback checks its current pool task ID. Independently, each module receives four milestone notifications and checks their payload sequence and delivery thread.

## Reproduce on Linux

Use an x86-64 Linux host with at least eight worker threads, GCC and its ASan and TSan runtimes, GNU binutils, pkg-config, Python 3.11 or newer, and SCons. Run each command from a separate clean copy of the source tree under test. Both configurations produce the same Godot filename.

```sh
python3 tests/sandbox_harnesses/threading/verify.py tsan /tmp/gate2-tsan --jobs 5
python3 tests/sandbox_harnesses/threading/verify.py asan /tmp/gate2-asan --jobs 3
```

The runner builds a threaded headless Linux debug template with the doctest suite. It checks the executable's linked sanitizer library and runs the single Gate 2 case in a temporary user-data directory. Each output directory contains the build log, the complete doctest and sanitizer output, and a JSON report with the compiler, flags, executable hash and relevant source hashes.

The runner requires a first wave of eight, 80,000 dispatched tasks, successful doctest assertions and a clean sanitizer exit. It enables LeakSanitizer for the ASan run and supplies no suppressions. A regular engine unit-test run on a smaller machine exercises as many simultaneous workers as the pool provides.

<!-- verification-evidence:start -->
## Verification evidence

Checked on 2026-11-23 at 14:29 UTC with GCC 15.2.0 and SCons 4.11.1 on x86-64 Ubuntu under WSL2. Gate 2 passed in both instrumented builds. All 11 recorded source hashes matched between builds and against the workspace.

| Run | Observed result | Evidence |
| --- | --- | --- |
| ThreadSanitizer | All 65 assertions passed with eight overlapping workers, 80,000 successful task waits, zero nested-wait violations and zero outstanding task handles. The process exited 0 with no race report. | [Build flags and hashes](results/tsan/result.json), [raw output](results/tsan/doctest.txt) |
| AddressSanitizer with leak detection | The same workload passed all 65 assertions, including four ordered milestone deliveries on the main thread for each module. The process exited 0 with no address error or leak report. | [Build flags and hashes](results/asan/result.json), [raw output](results/asan/doctest.txt) |

Both runs completed the Gate 2 workload without custom sanitizer suppressions.
<!-- verification-evidence:end -->
