# Fuzzing

`arranger_fuzz` changes real files at random and feeds them to the file
readers and the MCP server. A reader may refuse a bad file with an error. It
must never crash, hang, run out of memory, or read a song that does not save
and load again.

```sh
cmake -S . -B build-asan -DARRANGER_BUILD_APP=OFF -DARRANGER_SANITIZE=address,undefined
cmake --build build-asan --target arranger_fuzz
build-asan/tests/arranger_fuzz --list
build-asan/tests/arranger_fuzz style --seconds 600 --out findings
```

Each normal test run fuzzes every target for 5 seconds
(`ARRANGER_FUZZ_SECONDS`). The nightly workflow `fuzz-nightly.yml` runs each
target for 10 minutes.

When the fuzzer finds a problem, it saves the input in the out folder
(`crash-*`, `finding-*` or `slow-*`). To keep it as a regression test, fix the
bug, then copy the file into `regressions/<target>/` with a name that says what
it was. Every test run replays these files.
