# Deluge crash reporting for GDB builds without Python support.
# Source after loading the matching firmware ELF. Break at the first instruction
# so r0-r3 still contain the ARM ABI arguments, even in optimized builds.
# Keep the normal breakpoint stop notification: `silent` can produce a bare
# GDB/MI *stopped event without the reason/thread fields Cortex-Debug needs.

define deluge-crash-address
  printf "0x%08x: ", (unsigned int)$arg0
  # Bit zero marks Thumb code; clear it for symbol and source-line lookup.
  set $deluge_lookup = (unsigned int)$arg0 & 0xfffffffe
  info symbol $deluge_lookup
  info line *$deluge_lookup
end

define deluge-crash-report
  printf "\n=== Deluge crash report ===\n"
  printf "Fault type: 0x%x (0=undefined, 1=prefetch, 2=data abort, 3=reserved, e=software freeze, f=SVC)\n", $deluge_fault_type
  if $deluge_fault_type != 14
    printf "Faulting instruction:\n"
    deluge-crash-address $deluge_fault_pc
  end

  printf "Saved return address (LR): 0x%08x; saved stack pointer (SP): 0x%08x\n", $deluge_saved_lr, $deluge_saved_sp
  printf "\n"
  printf "Target remains paused before the fault handler runs. Continue to let it display the pad crash report.\n"
  printf "GDB backtrace (separate from the captured fault location; exception entry may prevent unwinding):\n"
  backtrace 16
end

# Sourcing this file again updates commands without adding duplicate breakpoints.
init-if-undefined $deluge_crash_installed = 0
if !$deluge_crash_installed
  break *handle_cpu_fault
  commands
    set $deluge_fault_pc = (unsigned int)$r0
    set $deluge_fault_type = (unsigned int)$r1
    set $deluge_saved_lr = (unsigned int)$r2
    set $deluge_saved_sp = (unsigned int)$r3
    deluge-crash-report
  end

  break *fault_handler_print_freeze_pointers
  commands
    set $deluge_fault_pc = 0
    set $deluge_fault_type = 14
    set $deluge_saved_lr = (unsigned int)$r0
    set $deluge_saved_sp = (unsigned int)$r1
    deluge-crash-report
  end
  set $deluge_crash_installed = 1
  printf "Deluge crash reporting enabled (CPU faults and software freezes).\n"
end

document deluge-crash-report
Print the most recently captured Deluge fault location, saved LR/SP, and GDB backtrace.
Requires the matching firmware ELF and readable target memory. Does not resume
the target or write target registers/memory. Does not scan the stack for callers.
end
