# RTL layout stability: the bbox x-positions of characters of an RTL
# line must NOT change when a tag (here "sel") splits the line into
# chunks.  Reproducer from the Tk ticket report, with full diagnostics.
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}
pack [text .t -font {{DejaVu Sans} 14}]
.t insert end "שלום עליכם, זהו טקסט מימין לשמאל"
update
set n [.t count -chars 1.0 1.end]
proc probe {} {
    global n
    set r {}
    for {set i 0} {$i < $n} {incr i} {
        set b [.t bbox 1.$i]
        lappend r [expr {$b eq "" ? -1 : [lindex $b 0]}]
    }
    return $r
}
set a [probe]
puts "nchars=$n"
puts "x before sel: $a"
puts "dlineinfo before: [.t dlineinfo 1.0]"
.t tag add sel 1.4 1.8
update
set b [probe]
puts "x after sel:  $b"
puts "dlineinfo after:  [.t dlineinfo 1.0]"
set diff {}
for {set i 0} {$i < $n} {incr i} {
    if {[lindex $a $i] ne [lindex $b $i]} {lappend diff $i}
}
puts "indices whose x moved: $diff"
if {[llength $diff] > 0} {
    puts "FAIL: the sel tag scrambles the RTL layout (dancing selection bug)"
    exit 1
}
puts "PASS: RTL layout is stable under the sel tag"
exit 0
