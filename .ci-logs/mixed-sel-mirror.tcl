# mixed-sel-mirror.tcl -- Diagnose Kevin's remaining bug 2:
# "on Windows, mixed bidi text shimmers when selected; the 'sel' tag
# appears to be rendered in mirror relation to the cursor and text".
#
# Portable (runs on X11 and Windows). Dumps, for three scenarios
# (RTL-start mixed, LTR-start mixed, pure RTL), the bbox of every index
# before and while a sel tag is applied, plus the ::tkBidiLog produced by
# BidiReorderChunks ("BD2" lines). On Windows, build with the
# winshape-diag patch and run with TK_DEBUG_WINSHAPE=1 to also get the
# WINSHAPE lines showing what Uniscribe is asked to draw and where.
#
# The test passes when applying sel does not move any index (a moved
# index means the selection redraw layed the line out differently =
# "shimmer").

fconfigure stdout -buffering none
package require Tk
wm withdraw .

set ::tkBidiLog ""

text .t -font {Arial 14} -width 60 -height 5 -wrap none -bd 0 \
    -highlightthickness 0 -selectbackground yellow -setgrid 0
pack .t

set L1 "\u05e2\u05d1\u05e8\u05d9\u05ea hello \u05e2\u05d5\u05dc\u05dd"
set L2 "hello \u05e2\u05d5\u05dc\u05dd world"
set L3 "\u05e2\u05d1\u05e8\u05d9\u05ea \u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd"

.t insert 1.0 "$L1\n$L2\n$L3"
update idletasks
after 300

# bbox collector: dict tag -> line.i -> list {x w}
array set BB [list]

proc collect {tag} {
    global BB
    foreach line {1 2 3} {
        set n [.t count $line.0 $line.end]
        for {set i 0} {$i < $n} {incr i} {
            set bb [.t bbox $line.$i]
            if {$bb ne ""} {
                lassign $bb x y w h
                puts "TKBBOX $tag $line.$i $x $w"
                set BB($tag,$line.$i) [list $x $w]
            }
        }
        puts "DLINFO $tag $line [.t dlineinfo $line.0]"
    }
}

proc dumplog {} {
    puts "BD2LOG-BEGIN"
    foreach l [split $::tkBidiLog "\n"] {
        if {[string trim $l] ne ""} { puts "BD2 [string trim $l]" }
    }
    puts "BD2LOG-END"
}

puts "SCENARIO base (no selection)"
collect base
update idletasks

set fails 0
foreach line {1 2 3} {
    set n [.t count $line.0 $line.end]
    set s [expr {$n > 12 ? 3 : 1}]
    set e [expr {$n > 6 ? $n - 3 : $n}]
    if {$e <= $s} { set s 0 ; set e $n }

    set ::tkBidiLog ""
    .t tag add sel $line.$s $line.$e
    update idletasks
    after 200
    puts "SCENARIO sel-line$line range $line.$s $line.$e"
    collect sel$line
    dumplog
    .t tag remove sel 1.0 end
    update idletasks
    after 100
}

# Compare base vs sel: a moved x means the selection redraw changed the
# layout of the line (the shimmer).
foreach line {1 2 3} {
    set moved {}
    foreach idx [array names BB base,$line.*] {
        set i [lindex [split $idx ,] 1]
        if {![info exists BB(sel$line,$i)]} { continue }
        lassign $BB(base,$i) bx bw
        lassign $BB(sel$line,$i) sx sw
        if {abs($bx - $sx) > 2 || abs($bw - $sw) > 2} {
            lappend moved $i
        }
    }
    if {[llength $moved] > 0} {
        puts "FAIL: line $line sel moved x of indices: $moved"
        incr fails
    } else {
        puts "PASS: line $line layout stable under sel"
    }
}

if {$fails > 0} {
    puts "RESULT: FAIL ($fails)"
} else {
    puts "RESULT: PASS"
}
exit 0
