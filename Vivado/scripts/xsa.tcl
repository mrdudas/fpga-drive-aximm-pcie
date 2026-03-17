# Opsero Electronic Design Inc. Copyright 2023
#
# This script runs synthesis, implementation and exports the hardware for a project.
#
# This script requires the target name and number of jobs to be specified upon launch.
# It can be lauched in two ways:
#
#   1. Using two arguments passed to the script via tclargs.
#      eg. vivado -mode batch -source xsa.tcl -notrace -tclargs <target-name> <jobs>
#
#   2. By setting the target variables before sourcing the script.
#      eg. set target <target-name>
#          set jobs <number-of-jobs>
#          source xsa.tcl -notrace
#
#*****************************************************************************************

# Check the version of Vivado used
set version_required "2024.2"
set ver [lindex [split $::env(XILINX_VIVADO) /] end]
if {![string equal $ver $version_required]} {
  puts "###############################"
  puts "### Failed to build project ###"
  puts "###############################"
  puts "This project was designed for use with Vivado $version_required."
  puts "You are using Vivado $ver. Please install Vivado $version_required,"
  puts "or download the project sources from a commit of the Git repository"
  puts "that was intended for your version of Vivado ($ver)."
  return
}

if { $argc == 2 } {
  set target [lindex $argv 0]
  puts "Target for the build: $target"
  set jobs [lindex $argv 1]
  puts "Number of jobs: $jobs"
} elseif { [info exists target] } {
  puts "Target for the build: $target"
  if { ![info exists jobs] } {
    set jobs 8
  }
} else {
  puts ""
  puts "This script runs synthesis, implementation and exports the hardware for a project."
  puts "It can be launched in two ways:"
  puts ""
  puts "  1. Using two arguments passed to the script via tclargs."
  puts "     eg. vivado -mode batch -source xsa.tcl -notrace -tclargs <target-name> <jobs>"
  puts ""
  puts "  2. By setting the target variables before sourcing the script."
  puts "     eg. set target <target-name>"
  puts "         set jobs <number-of-jobs>"
  puts "         source xsa.tcl -notrace"
  return
}

set design_name ${target}
set block_name fpgadrv

# Set the reference directory for source file relative paths
set script_dir [file dirname [file normalize [info script]]]
set origin_dir [file normalize [file join $script_dir ..]]

# Set the directory path for the original project from where this script was exported
set orig_proj_dir "[file normalize "$origin_dir/$design_name"]"

# Open project
open_project [file join $origin_dir $design_name ${design_name}.xpr]

proc run_completed {run_name} {
  set run [get_runs $run_name]
  set status [string trim [get_property STATUS $run]]
  set progress [string trim [get_property PROGRESS $run]]
  return [expr {$progress == "100%" && [string match "*Complete*" $status]}]
}

proc stale_running_run {run_name} {
  set run [get_runs $run_name]
  set status [string trim [get_property STATUS $run]]
  if {![string match "*Running*" $status] && ![string match "*Queued*" $status] && ![string match "*Scripts Generated*" $status]} {
    return 0
  }

  set run_dir [string trim [get_property DIRECTORY $run]]
  if {$run_dir == "" || ![file isdirectory $run_dir]} {
    return 0
  }

  set queue_files [glob -nocomplain -directory $run_dir .*.queue.rst]
  if {[llength $queue_files] == 0} {
    return 0
  }

  set queue_files_empty 1
  foreach queue_file $queue_files {
    if {[file size $queue_file] > 0} {
      set queue_files_empty 0
      break
    }
  }

  set run_logs [glob -nocomplain -directory $run_dir *.log]
  return [expr {$queue_files_empty && [llength $run_logs] == 0}]
}

proc failed_run {run_name} {
  set run [get_runs $run_name]
  set status [string trim [get_property STATUS $run]]
  return [expr {[string match -nocase "*fail*" $status] || [string match -nocase "*error*" $status]}]
}

proc run_requires_reset {run_name} {
  set run [get_runs $run_name]
  set status [string trim [get_property STATUS $run]]
  return [expr {[string match -nocase "*reset*" $status] || [string match -nocase "*out-of-date*" $status] || [string match -nocase "*stale*" $status]}]
}

proc reset_synth_subruns_missing_scripts {} {
  foreach run [get_runs *_synth_1] {
    set run_name [string trim [get_property NAME $run]]
    if {$run_name == "synth_1"} {
      continue
    }

    if {[run_completed $run_name]} {
      continue
    }

    set run_dir [string trim [get_property DIRECTORY $run]]
    if {$run_dir == "" || ![file isdirectory $run_dir]} {
      continue
    }

    if {![file exists [file join $run_dir runme.sh]]} {
      continue
    }

    set run_tcls [glob -nocomplain -directory $run_dir *.tcl]
    if {[llength $run_tcls] == 0} {
      puts "INFO: $run_name has no generated run Tcl script. Resetting run to regenerate scripts."
      reset_run $run_name
    }
  }
}

proc launch_or_resume_run {run_name args} {
  set run [get_runs $run_name]
  set status [string trim [get_property STATUS $run]]
  set progress [string trim [get_property PROGRESS $run]]
  puts "INFO: $run_name initial status: $status (progress: $progress)"

  if {[run_completed $run_name]} {
    puts "INFO: $run_name is already complete. Reusing existing results."
    return
  }

  if {[failed_run $run_name]} {
    puts "INFO: $run_name previously failed. Resetting run state before relaunch."
    reset_run $run_name
  }

  if {[run_requires_reset $run_name]} {
    puts "INFO: $run_name status indicates reset is required. Resetting run before launch."
    reset_run $run_name
  }

  if {[stale_running_run $run_name]} {
    puts "INFO: $run_name is marked active, but its run directory has no logs. Resetting the stale run state."
    reset_run $run_name
  }

  if {[catch {launch_runs $run_name {*}$args} launch_err]} {
    if {[string match "*already running and cannot be relaunched*" $launch_err]} {
      if {[stale_running_run $run_name]} {
        puts "INFO: $run_name still appears stale after launch attempt. Resetting and relaunching."
        reset_run $run_name
        launch_runs $run_name {*}$args
      } else {
        puts "INFO: $run_name is already running. Waiting for it to finish."
      }
    } elseif {[string match "*needs to be reset before launching*" $launch_err]} {
      puts "INFO: $run_name requires reset before launch. Resetting and relaunching."
      reset_run $run_name
      launch_runs $run_name {*}$args
    } else {
      return -code error $launch_err
    }
  }

  wait_on_run $run_name

  set final_status [string trim [get_property STATUS $run]]
  set final_progress [string trim [get_property PROGRESS $run]]
  puts "INFO: $run_name final status: $final_status (progress: $final_progress)"

  if {![run_completed $run_name]} {
    return -code error "Run $run_name did not complete successfully. Final status: $final_status (progress: $final_progress)"
  }
}

reset_synth_subruns_missing_scripts
launch_or_resume_run synth_1 -jobs $jobs
launch_or_resume_run impl_1 -jobs $jobs -to_step write_bitstream
write_hw_platform -fixed -include_bit -force -file $origin_dir/$design_name/${block_name}_wrapper.xsa
validate_hw_platform -verbose $origin_dir/$design_name/${block_name}_wrapper.xsa

