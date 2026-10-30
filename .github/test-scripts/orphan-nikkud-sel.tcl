# orphan-nikkud-sel.tcl
# Ticket [8504cdc7c8]: "selection drawn shifted left and overlapping
# existing text when selecting right of a base-less vowel point".
# Headless reproducer: per-index bboxes, bbox/index round-trip, cell
# monotonicity, and a simulated mouse drag selection over the text to
# the right of the orphan mark.  No ICU dependency.
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}

set ::failures 0
proc fail {msg} {
    incr ::failures
    puts "FAIL: $msg"
}

pack [text .t -font {{DejaVu Sans} 16} -wrap none -borderwidth 0 \
    -highlightthickness 0 -padx 0 -pady 0]
# U+05B0 (sheva) with no base consonant, then ASCII text to select.
.t insert 1.0 "\u05B0 select the text to the right of me"
update

set n [.t count -chars 1.0 1.end]
puts "nchars=$n line='[.t get 1.0 1.end]'"
puts "dlineinfo: [.t dlineinfo 1.0]"

# --- Dump per-index bboxes -------------------------------------------------
for {set i 0} {$i < $n} {incr i} {
    puts "  bbox $i: [.t bbox 1.$i]  char=[scan [.t get 1.$i] %c]"
}

# --- Check 1: bbox -> index -> bbox round-trip -----------------------------
# A zero-advance orphan mark legitimately shares the cell of the next
# character, so for it (and the space right after) we tolerate landing on
# an adjacent index; base characters must round-trip exactly.
for {set i 0} {$i < $n} {incr i} {
    set b [.t bbox 1.$i]
    if {$b eq ""} continue
    lassign $b bx by bw bh
    set back [.t index @[expr {$bx + $bw/2}],[expr {$by + $bh/2}]]
    if {[scan $back "1.%d" d bi] != 1} continue
    set ch [scan [.t get 1.$i] %c]
    if {($ch >= 0x05B0 && $ch <= 0x05C7) || $ch == 32} {
        if {$bi < $i - 1 || $bi > $i + 1} {
            fail "round-trip: index 1.$i (char U+[format %04X $ch]) center maps to $back"
            break
        }
    } elseif {$bi != $i && $bi != $i+1} {
        fail "round-trip: index 1.$i (char [format %c $ch]) center maps to $back, expected 1.$i"
        break
    }
}
if {!$::failures} { puts "PASS: bbox/index round-trip consistent" }

# --- Check 2: cells do not go backwards or overlap --------------------------
set prevRight -1
for {set i 0} {$i < $n} {incr i} {
    set b [.t bbox 1.$i]
    if {$b eq ""} continue
    lassign $b bx by bw bh
    if {$bw > 0 && $bx + $bw > $prevRight} {
        set prevRight [expr {$bx + $bw}]
    }
    if {$bx + $bw < $prevRight - 1} {
        fail "monotonicity: index 1.$i right edge [expr {$bx+$bw}] < previous right edge $prevRight (selection geometry diverges from glyph positions)"
        break
    }
}
if {!$::failures} { puts "PASS: cell geometry monotonic, no overlap" }

# --- Check 3: simulated mouse drag over the ASCII text ----------------------
# Press at the center of the 't' of "text" (1.13), drag to the 'h' of
# "right" (1.29), release.  The selection must cover roughly [1.13, 1.30).
set bp [.t bbox 1.13]
set bq [.t bbox 1.29]
if {$bp eq "" || $bq eq ""} {
    fail "cannot locate press/release targets"
} else {
    lassign $bp pxx pyy pw ph
    lassign $bq qxx qyy qw qh
    set Y [expr {$pyy + $ph/2}]
    set x1 [expr {$pxx + $pw/2}]
    set x2 [expr {$qxx + $qw/2}]
    focus .t
    event generate .t <ButtonPress-1> -x $x1 -y $Y
    update
    for {set x $x1} {$x <= $x2} {set x [expr {$x + 10}]} {
        if {$x > $x2} { set x $x2 }
        event generate .t <B1-Motion> -x $x -y $Y
    }
    event generate .t <ButtonRelease-1> -x $x2 -y $Y
    update
    set ranges [.t tag ranges sel]
    puts "sel ranges: $ranges"
    if {[llength $ranges] != 2} {
        fail "drag selection produced [llength $ranges] range elements: $ranges (expected exactly one range covering 1.13..1.30)"
    } else {
        scan [lindex $ranges 0] "1.%d" d s1
        scan [lindex $ranges 1] "1.%d" d e1
        if {$s1 < 12 || $s1 > 14 || $e1 < 29 || $e1 > 31} {
            fail "drag selection covers 1.$s1..1.$e1, expected about 1.13..1.30 (selection boundary off by more than one character: geometry shifted)"
        } else {
            puts "PASS: drag selection covers the expected range (1.$s1..1.$e1)"
        }
    }
}

# --- Summary ----------------------------------------------------------------
if {$::failures} {
    puts "RESULT: FAIL ($::failures)"
    exit 1
}
puts "RESULT: PASS"
exit 0
