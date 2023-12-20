if {"::tcltest" ni [namespace children]} {
	package require tcltest
	namespace import ::tcltest::*
}

::tcltest::loadTestedCommands
package require lambdaric

tcltest::testConstraint testMode [expr {
	[llength [info commands ::lambdaric::_testmode]] > 0 &&
	[lambdaric::_testmode]
}]

source [file join [file dirname [info script]] events.tcl]
