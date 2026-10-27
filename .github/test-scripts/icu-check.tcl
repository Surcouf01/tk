# Informational: report whether libicu was found (tk::startOfCluster).
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}
if {[llength [info commands ::tk::startOfCluster]]} {
    puts "ICU: available (tk::startOfCluster present)"
} else {
    puts "ICU: NOT available - cluster test will report SKIP"
}
exit 0
