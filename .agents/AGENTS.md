# Rouen Project Rules

## Build Parallelism
- When compiling this project, limit parallel build jobs to at most 4 (e.g., use `cmake --build build --parallel 4` or `make -j4`) because the memory on this computer cannot match a parallel build with all its cores.
