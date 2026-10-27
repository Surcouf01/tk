# RTL layout stability: the bbox x-positions of characters of an RTL
# line must NOT change when a tag (here "sel") splits the line into
# chunks.  Reproducer from the Tk ticket report.
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}
pack [text .t -font {{DejaVu Sans} 14}]
.t insert end "שלום עליכם, זהו טקסט מימין לשמאל"
update
set a {}
foreach i {0 4 8} {lappend a [lindex [.t bbox 1.$i] 0]}
.t tag add sel 1.4 1.8
update
set b {}
foreach i {0 4 8} {lappend b [lindex [.t bbox 1.$i] 0]}
puts "positions before sel tag: $a"
puts "positions after sel tag:  $b"
if {$a ne $b} {
    puts "FAIL: the sel tag scrambles the RTL layout (dancing selection bug)"
    exit 1
}
puts "PASS: RTL layout is stable under the sel tag"
exit 0
