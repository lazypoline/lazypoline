# lazypoline

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.10372035.svg)](https://doi.org/10.5281/zenodo.10372035)

**lazypoline** is a fast, exhaustive, and expressive syscall interposer for user-space Linux applications. It uses a _hybrid interposition_ mechanism based on Syscall User Dispatch (SUD) and binary rewriting to exhaustively interpose all syscalls with maximal efficiency. You can find more details in our DSN'24 paper, ["System Call Interposition Without Compromise"](https://adriaanjacobs.github.io/files/dsn24lazypoline.pdf).

```bibtex
@inproceedings{jacobs2024lazypoline,
  title={System Call Interposition Without Compromise},
  author={Jacobs, Adriaan and G{\"u}lmez, Merve and Andries, Alicia and Volckaert, Stijn and Voulimeneas, Alexios},
  booktitle={Proceedings of the International Conference on Dependable Systems and Networks},
  year={2024}
}
```

## Building
We use CMake for building. Typical usage as follows:
```bash
mkdir build
cd build
cmake ../
make -j
```

## Running
lazypoline can hook syscalls in precompiled binaries by setting the appropriate environment variables when launching. Example:
```bash
# from project source
LIBLAZYPOLINE="$(realpath build)/liblazypoline.so" LD_PRELOAD="$(realpath build)/libbootstrap.so" <some binary>
```

Note that this way of launching will miss syscalls performed before and while the dynamic loader loads lazypoline.
Just like zpoline, lazypoline requires permissions to `mmap` at low virtual addresses, i.e., the 0 page. You can permit this via:
```bash
echo 0 | sudo tee /proc/sys/vm/mmap_min_addr
```

## Extending
You can modify lazypoline to better fit your needs. `syscall_emulate` in [lazypoline.cpp](/lazypoline.cpp) is your main entry point. 

We recommend including lazypoline into your application through the small [bootstrap](/bootstrap_runtime.cpp), which uses `dlmopen` to load the main lazypoline library in a new dynamic library namespace. This ensures that the interposer links to a separate copy of all libraries it uses, instead of re-using those from the application it is interposing. Application libraries may have re-written syscalls, which would lead to recursive interposer invocations. In addition, many library functions that perform syscalls are not re-entrancy safe, which can lead to hard-to-diagnose bugs when they are invoked from the interposer. 

## Configuration
[config.h](/config.h) contains some options to control lazypoline's behavior. Most are self-explanatory. Enable `COMPAT_NONDEP_APP` to restore page permissions to `RWX` instead of `RX` (default). Some old JIT engines, like [`tcc`](https://bellard.org/tcc/) require this.

## Compatibility
lazypoline is well-tested on Ubuntu 20.04 and 22.04. Its primary compatibility requirement is kernel version >= 5.11 (needs SUD). In addition, the `SIGSETSIZE` in [util.h](/util.h) and everything under [sysdeps/](/sysdeps/) should be kept in sync with whatever glibc version you're running. 

## Related DSN 2024 artifacts
You can find the benchmarks for the performance evaluation in the DSN paper at [https://github.com/lazypoline/benchmarks](https://github.com/lazypoline/benchmarks). The Pintool to track application's register preservation expectations across syscalls is located at [https://github.com/lazypoline/pintool-syscall-abi-expectations](https://github.com/lazypoline/pintool-syscall-abi-expectations). 
