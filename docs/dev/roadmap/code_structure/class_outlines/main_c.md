# main.c
- `main.c` contains `main()`, which runs part of the boot sequence and calls `deluge_main()` in `deluge.cpp`
- `deluge_main()` runs the rest of the boot sequence and calls:
  -  `registerTasks()` and `startTaskManager()`, `#ifdef USE_TASK_MANAGER`
  -  `mainLoop()` otherwise
