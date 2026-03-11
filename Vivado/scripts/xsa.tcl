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
set version_required "2024.1"
set version_also_allowed "2024.2"
set ver [lindex [split $::env(XILINX_VIVADO) /] end]
if {![string equal $ver $version_required] && ![string equal $ver $version_also_allowed]} {
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

# Set the reference directory for source file relative paths (by default the value is script directory path)
set origin_dir "."

# Set the directory path for the original project from where this script was exported
set orig_proj_dir "[file normalize "$origin_dir/$design_name"]"

# Open project
open_project $origin_dir/$design_name/$design_name.xpr

# Generate wrapper if not already present
set wrapper_file "$origin_dir/$design_name/${design_name}.srcs/sources_1/bd/${block_name}/${block_name}_wrapper.v"
set wrapper_file_vhd "$origin_dir/$design_name/${design_name}.srcs/sources_1/bd/${block_name}/${block_name}_wrapper.vhd"
if {![file exists $wrapper_file] && ![file exists $wrapper_file_vhd]} {
  make_wrapper -files [get_files ${block_name}.bd] -top
  set wrapper [glob -nocomplain "$origin_dir/$design_name/${design_name}.gen/sources_1/bd/${block_name}/hdl/${block_name}_wrapper.v"]
  if {[llength $wrapper] == 0} {
    set wrapper [glob -nocomplain "$origin_dir/$design_name/${design_name}.gen/sources_1/bd/${block_name}/hdl/${block_name}_wrapper.vhd"]
  }
  add_files -norecurse $wrapper
  set_property top ${block_name}_wrapper [current_fileset]
  update_compile_order -fileset sources_1
}

# Generate all BD output products (IP wrappers, HDL, etc.)
generate_target all [get_files ${block_name}.bd]
export_ip_user_files -of_objects [get_files ${block_name}.bd] -no_script -sync -force -quiet

reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] != "100%"} {
  error "Synthesis failed"
}
reset_run impl_1
launch_runs impl_1 -jobs $jobs -to_step write_bitstream
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] != "100%"} {
  error "Implementation failed"
}
write_hw_platform -fixed -include_bit -force -file $origin_dir/$design_name/${block_name}_wrapper.xsa
validate_hw_platform -verbose $origin_dir/$design_name/${block_name}_wrapper.xsa

