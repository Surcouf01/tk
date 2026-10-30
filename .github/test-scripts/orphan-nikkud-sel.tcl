# orphan-nikkud-sel.tcl
# Ticket [8504cdc7c8]: "selection drawn shifted left and overlapping
# existing text when selecting right of a base-less vowel point".
#
# Reproducer based on Genesis 38:1 (Hebrew): the line starts with a
# base-less vowel point -- the patach U+05B7 of the first word, its base
# consonant absent, as in the ticket -- followed by a bit of LTR text to
# the right of the mark (the verse reference "Gen. 38:1"), then the
# Hebrew verse itself.  Checks: per-index bboxes, layout direction,
# bbox/index round-trip, cell overlap, and a simulated mouse-drag
# selection over the LTR reference.  Geometry checks are deliberately
# restricted to the region left of the verse so that unrelated,
# separately ticketed strong-RTL/cluster issues do not mask the
# direction bug reported here.
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}

set ::failures 0
proc fail {msg} {
    incr ::failures
    puts "FAIL: $msg"
}

# Hebrew combining marks: points, accents, cantillation signs (Bidi_Class NSM).
proc isMark {ch} {
    expr {($ch >= 0x0591 && $ch <= 0x05BD) || $ch == 0x05BF
        || ($ch >= 0x05C1 && $ch <= 0x05C2)
        || ($ch >= 0x05C4 && $ch <= 0x05C5) || $ch == 0x05C7}
}

pack [text .t -font {{DejaVu Sans} 16} -wrap none -borderwidth 0 \
    -highlightthickness 0 -padx 0 -pady 0]

# Genesis 38:1: base-less patach, then the LTR reference, then the verse.
set line "\u05B7 Gen. 38:1 \u2014 \u05D5\u05B7\u05D9\u05B0\u05D4\u05B4\u05D9\u0020\u05D1\u05B8\u05BC\u05E2\u05B5\u05EA\u0020\u05D4\u05B7\u05D4\u05B4\u05D5\u05D0\u0020\u05D5\u05B7\u05D9\u05B5\u05BC\u05E8\u05B6\u05D3\u0020\u05D9\u05B0\u05D4\u05D5\u05BC\u05D3\u05B8\u05D4\u0020\u05DE\u05B5\u05D0\u05B5\u05EA\u0020\u05D0\u05B6\u05D7\u05B8\u05D9\u05D5\u0020\u05D5\u05B7\u05D9\u05B5\u05BC\u05D8\u0020\u05E2\u05B7\u05D3\u05BE\u05D0\u05B4\u05D9\u05E9\u05C1\u0020\u05E2\u05B2\u05D3\u05BB\u05DC\u05B8\u05BC\u05DE\u05B4\u05D9\u0020\u05D5\u05BC\u05E9\u05B0\u05C1\u05DE\u05D5\u05B9\u0020\u05D7\u05B4\u05D9\u05E8\u05B8\u05D4\u05C3"
.t insert 1.0 $line
update

set n [.t count -chars 1.0 1.end]
set ref "Gen. 38:1"
set gs [string first $ref $line]
set glen [string length $ref]
set gLast [expr {$gs + $glen - 1}]
set vs [string first "\u05D5\u05B7\u05D9\u05B0\u05D4\u05B4\u05D9" $line]
puts "nchars=$n line='[.t get 1.0 1.end]'"
puts "ref at 1.$gs..1.[expr {$gs + $glen}]  verse starts at 1.$vs"
puts "dlineinfo: [.t dlineinfo 1.0]"

# --- Dump per-index bboxes -------------------------------------------------
for {set i 0} {$i < $n} {incr i} {
    puts "  bbox $i: [.t bbox 1.$i]  char=[scan [.t get 1.$i] %c]"
}

# --- Check 1a: the LTR reference is laid out left-to-right ----------------
# The orphan patach U+05B7 is a combining mark (Bidi_Class NSM): per
# UAX #9 (rules P2/W1) it is not a strong character and must not decide
# the direction.  The first strong character is 'G' (L), so the reference
# must be laid out in logical order, left to right.
set prevX -1
set dirOK 1
for {set i $gs} {$i <= $gLast} {incr i} {
    set b [.t bbox 1.$i]
    if {$b eq ""} continue
    lassign $b bx by bw bh
    if {$bx < $prevX - 1} {
        fail "direction: index 1.$i is at x=$bx, left of the previous index at x=$prevX although the first strong character is 'G' (L): the orphan NSM mark U+05B7 flipped the chunk direction (whole line laid out RTL)"
        set dirOK 0
        break
    }
    set prevX $bx
}
if {$dirOK} { puts "PASS: LTR reference laid out left-to-right (NSM mark did not flip direction)" }

# --- Check 1b: the Hebrew verse sits right of the LTR reference -----------
set bv [.t bbox 1.$vs]
set bl [.t bbox 1.$gLast]
if {$bv eq "" || $bl eq ""} {
    fail "cannot locate verse start or reference end"
} elseif {[lindex $bv 0] > [lindex $bl 0]} {
    puts "PASS: Hebrew verse laid out to the right of the LTR reference (LTR base direction)"
} else {
    fail "chunk order: verse starts at x=[lindex $bv 0], not right of the LTR reference ending at x=[lindex $bl 0] (line flipped RTL by the orphan NSM mark)"
}

# --- Check 2: bbox -> index -> bbox round-trip -----------------------------
# Restricted to the region left of the Hebrew verse (mark, reference,
# dash): zero-advance marks legitimately share a neighbouring cell, so
# for them (and spaces) we tolerate landing on an adjacent index; base
# characters must round-trip exactly.
set rtOK 1
for {set i 0} {$i < $vs} {incr i} {
    set b [.t bbox 1.$i]
    if {$b eq ""} continue
    lassign $b bx by bw bh
    set back [.t index @[expr {$bx + $bw/2}],[expr {$by + $bh/2}]]
    if {![regexp {^1\.(\d+)$} $back -> bi]} continue
    set ch [scan [.t get 1.$i] %c]
    if {[isMark $ch] || $ch == 32} {
        if {$bi < $i - 1 || $bi > $i + 1} {
            fail "round-trip: index 1.$i (char U+[format %04X $ch]) center maps to $back"
            set rtOK 0
            break
        }
    } elseif {$bi != $i && $bi != $i+1} {
        fail "round-trip: index 1.$i (char U+[format %04X $ch]) center maps to $back, expected 1.$i"
        set rtOK 0
        break
    }
}
if {$rtOK} { puts "PASS: bbox/index round-trip consistent" }

# --- Check 3: base character cells do not overlap --------------------------
set cells {}
for {set i 0} {$i < $vs} {incr i} {
    set ch [scan [.t get 1.$i] %c]
    if {[isMark $ch]} continue
    set b [.t bbox 1.$i]
    if {$b eq ""} continue
    lassign $b bx by bw bh
    if {$bw > 2} { lappend cells [list $bx [expr {$bx + $bw}] $i] }
}
set cells [lsort -integer -index 0 $cells]
set prevRight -1
set ovOK 1
foreach c $cells {
    lassign $c cl cr ci
    if {$cl < $prevRight - 1} {
        fail "overlap: index 1.$ci cell \[$cl,$cr) overlaps previous cell ending at $prevRight (selection geometry diverges from glyph positions)"
        set ovOK 0
        break
    }
    set prevRight $cr
}
if {$ovOK} { puts "PASS: cell geometry consistent, no overlap" }

# --- Check 4: simulated mouse drag over the LTR reference ------------------
# Press at the center of the 'G' of "Gen.", drag to the '1' of "38:1",
# release.  The selection must cover roughly [1.$gs, 1.[expr {$gs+$glen}]).
set bp [.t bbox 1.$gs]
set bq [.t bbox 1.$gLast]
if {$bp eq "" || $bq eq ""} {
    fail "cannot locate press/release targets"
} else {
    lassign $bp pxx pyy pw ph
    lassign $bq qxx qyy qw qh
    set Y [expr {$pyy + $ph/2}]
    set x1 [expr {$pxx + $pw/2}]
    set x2 [expr {$qxx + $qw/2}]
    set step 10
    if {$x2 < $x1} { set step -10 }
    focus .t
    event generate .t <ButtonPress-1> -x $x1 -y $Y
    update
    set x $x1
    while {($step > 0 && $x < $x2) || ($step < 0 && $x > $x2)} {
        set x [expr {$x + $step}]
        if {($step > 0 && $x > $x2) || ($step < 0 && $x < $x2)} { set x $x2 }
        event generate .t <B1-Motion> -x $x -y $Y
    }
    event generate .t <ButtonRelease-1> -x $x2 -y $Y
    update
    set ranges [.t tag ranges sel]
    puts "sel ranges: $ranges"
    if {[llength $ranges] != 2} {
        fail "drag selection produced [llength $ranges] range elements: $ranges (expected exactly one range covering 1.$gs..1.[expr {$gs + $glen}])"
    } else {
        regexp {^1\.(\d+)$} [lindex $ranges 0] -> s1
        regexp {^1\.(\d+)$} [lindex $ranges 1] -> e1
        if {$s1 < $gs - 1 || $s1 > $gs + 1 || $e1 < $gs + $glen - 1 || $e1 > $gs + $glen + 1} {
            fail "drag selection covers 1.$s1..1.$e1, expected about 1.$gs..1.[expr {$gs + $glen}] (selection boundary off by more than one character: geometry shifted)"
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