# trace.gdb
set breakpoint pending on
set pagination off

# Break on the internal valist version (more reliable)
break g_signal_emit_valist
command
  # x86_64 registers: $rdi = object, $rsi = signal_id
  set $sig_name = (char*)g_signal_name($rsi)
  set $obj_type = (char*)g_type_name_from_instance($rdi)
  
  printf "Signal: [%s] on Object: %p (%s)\n", $sig_name, $rdi, $obj_type
  continue
end

# Start the program
run
