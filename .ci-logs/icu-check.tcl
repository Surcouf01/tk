# Informational: report whether libicu was found (tk::startOfCluster).
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}
if {[llength [info commands ::tk::startOfCluster]]} {
    puts "ICU: available (tk::startOfCluster present)"
} else {
    puts "ICU: NOT available - tk::startOfCluster missing"
    puts "tk commands: [lsort [info commands ::tk::*]]"
}
exit 0
