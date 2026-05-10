# OS Simulation in C

## Features
- 40-word fixed memory with per-process contiguous blocks.
- PCB in memory (`processID`, state, `programCounter`, lower/upper bounds).
- Interpreter for:
  - `print`, `assign`, `writeFile`, `readFile`, `printFromTo`
  - `semWait`, `semSignal`
- Three mutexes: `userInput`, `userOutput`, `file`.
- Global `readyQueue`, global `blockedQueue`, and per-mutex blocked queues.
- Scheduling algorithms: Round Robin, HRRN, and MLFQ (runtime choice).
- MLFQ policy: 3 levels with quanta `1`, `2`, and `4`; new and unblocked processes re-enter at their current level, and processes are demoted only after consuming a full time slice.
- Swapping to disk files when memory is full.

## Project layout
- `main.c` - entry point.
- `include/os.h` - shared types and function prototypes.
- `src/queue.c` - queue implementation.
- `src/utils.c` - helpers and lookups.
- `src/memory.c` - memory layout, variables, swapping.
- `src/process.c` - program loading and process creation.
- `src/interpreter.c` - instruction execution and system calls.
- `src/scheduler.c` - RR, HRRN, and MLFQ scheduler logic.
- `src/os.c` - system initialization and runtime input flow.

## Build
```bash
gcc -std=c11 -Wall -Wextra -pedantic -O2 -Iinclude main.c src/*.c -o os_sim
```

## Run

### Browser UI
On Windows, run:

```bat
run.bat
```

This builds `os_sim.exe`, starts a local web server on `http://localhost:8080/`, and opens the browser UI.

In the browser you can:
1. Choose `RR`, `HRRN`, or `MLFQ`
2. Set the RR quantum
3. Add program filenames and arrival times
4. Queue runtime input lines for any `assign ... input` instructions
5. Run the simulator and inspect the output in the page

### Terminal mode
You can still run the executable directly:

```bash
./os_sim
```

Then provide:
1. Scheduler: `RR`, `HRRN`, or `MLFQ`
2. Quantum (if RR)
3. Number of program files
4. Each filename and arrival time
