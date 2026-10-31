# caret-rtl-margin.tcl
# dancing_bidi remaining bug 1 (reported by Kevin, branch integrator):
# "the caret/cursor is inset one glyph at the start of a paragraph of
#  RTL text - you cannot insert it at the right margin."
#
# Observable, headless: the branch logs "BD2 caret x=%d" in the global
# variable ::tkBidiLog whenever the insertion cursor is displayed, and
# logs the chunk/unit geometry of the line.  We place the caret at the
# start of a pure RTL paragraph and check that:
#   A. the caret is drawn at the RIGHT margin of the line (its visual
#      position for the logically-first index of RTL text), not inset
#      by one glyph;
#   B. clicking in the blank area at the right margin (right of the
#      line extent) yields the paragraph start index 1.0, so the user
#      can insert at the right margin;
#   C. (control) with the caret at the logical end (1.end) the caret is
#      drawn at the LEFT margin of the line.
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}

set failures 0
proc fail {msg} {
    global failures
    incr failures
    puts "FAIL: $msg"
}

font create F -family "DejaVu Sans" -size 16
pack [text .t -font F -wrap none -borderwidth 0 -highlightthickness 0 \
    -padx 0 -pady 0]

.t insert 1.0 "שלום עולם טקסט בעברית"
.t mark set insert 1.0
update

set n [.t count -chars 1.0 1.end]
lassign [.t dlineinfo 1.0] dx dy dw dh dbase
puts "nchars=$n dlineinfo: $dx $dy $dw $dh $dbase"
for {set i 0} {$i < $n} {incr i} {
    puts "  bbox 1.$i: [.t bbox 1.$i] char=[scan [.t get 1.$i] %c]"
}

# --- Check A: caret at 1.0 sits at the right margin -----------------------
# Force a full redraw of the line, then read the caret x from the log.
set ::tkBidiLog {}
.t configure -height 2
update
.t configure -height 10
update

set caretX -1
foreach e $::tkBidiLog {
    if {[regexp {^BD2 caret x=(-?\d+)} $e -> cx]} {
        set caretX $cx
    }
}
if {$caretX < 0} {
    fail "A: no BD2 caret entry found in the layout log"
} else {
    set rightEdge [expr {$dx + $dw}]
    set inset [expr {$rightEdge - $caretX}]
    puts "caret at 1.0: x=$caretX rightEdge=$rightEdge inset=$inset"
    if {$inset > 3 || $inset < -3} {
        fail "A: caret at the start of the RTL paragraph is drawn at x=$caretX, inset $inset px from the right margin ($rightEdge); expected at the right margin (inset < one glyph)"
    } else {
        puts "PASS: A caret at 1.0 drawn at the right margin of the RTL paragraph"
    }
}

# --- Check B: clicking at the right margin inserts at 1.0 -----------------
set clickX [expr {$dx + $dw + 20}]
if {$clickX > [winfo width .t] - 10} {
    set clickX [expr {[winfo width .t] - 10}]
}
set ymid [expr {$dy + $dh/2}]
set idx [.t index @$clickX,$ymid]
puts "click at @$clickX,$ymid -> $idx"
scan $idx "%d.%d" _ ci
if {$ci != 0} {
    fail "B: clicking in the blank area at the right margin ($clickX,$ymid) gives $idx, expected 1.0 (start of the RTL paragraph) - cannot insert at the right margin"
} else {
    puts "PASS: B clicking at the right margin places the caret at 1.0"
}

# --- Check C (control): caret at 1.end sits at the left margin -------------
.t mark set insert 1.end
set ::tkBidiLog {}
.t configure -height 2
update
.t configure -height 10
update
set caretX2 -1
foreach e $::tkBidiLog {
    if {[regexp {^BD2 caret x=(-?\d+)} $e -> cx]} {
        set caretX2 $cx
    }
}
if {$caretX2 < 0} {
    fail "C: no BD2 caret entry found for the caret at 1.end"
} else {
    puts "caret at 1.end: x=$caretX2 leftEdge=$dx"
    if {$caretX2 > $dx + 3} {
        fail "C: caret at the logical end of the RTL paragraph is drawn at x=$caretX2, expected at the left margin ($dx)"
    } else {
        puts "PASS: C caret at 1.end drawn at the left margin"
    }
}

# Dump the full layout log for diagnosis
puts "--- tkBidiLog (last layout) ---"
foreach e $::tkBidiLog { puts "  $e" }

if {$failures} {
    puts "RESULT: FAIL ($failures)"
    exit 1
}
puts "RESULT: PASS"
exit 0