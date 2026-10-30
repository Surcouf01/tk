# orphan-nikkud-sel.tcl
# Ticket [8504cdc7c8]: "selection drawn shifted left and overlapping
# existing text when selecting right of a base-less vowel point".
#
# Reproducer based on Genesis 38:1.  Two lines:
#
#   line 1 (the bug trigger): a base-less vowel point -- the patach
#   U+05B7 of the first word of Genesis 38:1, its base consonant absent,
#   as in the ticket -- followed by LTR text to the right of the mark:
#   the verse reference "Gen. 38:1" and the first sentence of the JPS
#   translation.  The first strong character is 'G' (L), so the line
#   must be laid out left-to-right; the selection geometry must round-
#   trip, and a simulated mouse drag over the reference must select it.
#
#   line 2 (non-regression): the Hebrew text of Genesis 38:1 itself,
#   starting with a strong RTL letter.  It must stay laid out
#   right-to-left, i.e. the NSM fix must not make RTL lines flip.
#
# Line 1 deliberately contains no Hebrew: the separately ticketed
# "dancing selection" bug (sel tag scrambles layout of lines containing
# RTL runs, and point-to-index during an active selection jumps to end
# of line) would otherwise mask the direction bug reported here.
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

set line1 "\u05B7 Gen. 38:1 \u2014 And it came to pass at that time, that Judah went down from his brethren."
set line2 "\u05D5\u05B7\u05D9\u05B0\u05D4\u05B4\u05D9\u0020\u05D1\u05B8\u05BC\u05E2\u05B5\u05EA\u0020\u05D4\u05B7\u05D4\u05B4\u05D5\u05D0\u0020\u05D5\u05B7\u05D9\u05B5\u05BC\u05E8\u05B6\u05D3\u0020\u05D9\u05B0\u05D4\u05D5\u05BC\u05D3\u05B8\u05D4\u0020\u05DE\u05B5\u05D0\u05B5\u05EA\u0020\u05D0\u05B6\u05D7\u05B8\u05D9\u05D5\u0020\u05D5\u05B7\u05D9\u05B5\u05BC\u05D8\u0020\u05E2\u05B7\u05D3\u05BE\u05D0\u05B4\u05D9\u05E9\u05C1\u0020\u05E2\u05B2\u05D3\u05BB\u05DC\u05B8\u05BC\u05DE\u05B4\u05D9\u0020\u05D5\u05BC\u05E9\u05B0\u05C1\u05DE\u05D5\u05B9\u0020\u05D7\u05B4\u05D9\u05E8\u05B8\u05D4\u05C3"
.t insert 1.0 $line1\n$line2
update

set n1 [.t count -chars 1.0 1.end]
set n2 [.t count -chars 2.0 2.end]
set ref "Gen. 38:1"
set gs [string first $ref $line1]
set glen [string length $ref]
set gLast [expr {$gs + $glen - 1}]
puts "line1 nchars=$n1 line='[.t get 1.0 1.end]'"
puts "line2 nchars=$n2 line='[.t get 2.0 2.end]'"
puts "ref at 1.$gs..1.[expr {$gs + $glen}]"
puts "dlineinfo 1.0: [.t dlineinfo 1.0]"

# --- Dump per-index bboxes (both lines) ------------------------------------
for {set i 0} {$i < $n1} {incr i} {
    puts "  bbox 1.$i: [.t bbox 1.$i]  char=[scan [.t get 1.$i] %c]"
}
for {set i 0} {$i < $n2} {incr i} {
    puts "  bbox 2.$i: [.t bbox 2.$i]  char=[scan [.t get 2.$i] %c]"
}

# --- Check 1: the LTR sentence of line 1 is laid out left-to-right ---------
# The orphan patach U+05B7 is a combining mark (Bidi_Class NSM): per
# UAX #9 (rules P2/W1) it is not a strong character and must not decide
# the direction.  The first strong character is 'G' (L), so the text
# right of the mark must be laid out in logical order, left to right.
set prevX -1
set dirOK 1
for {set i 2} {$i < $n1} {incr i} {
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
if {$dirOK} { puts "PASS: LTR text of line 1 laid out left-to-right (NSM mark did not flip direction)" }

# --- Check 2: bbox -> index -> bbox round-trip (line 1) -------------------
# A zero-advance orphan mark legitimately shares the cell of the next
# character, so for it (and for spaces) we tolerate landing on an
# adjacent index; base characters must round-trip exactly.
set rtOK 1
for {set i 0} {$i < $n1} {incr i} {
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
        fail "round-trip: index 1.$i (char [format %c $ch]) center maps to $back, expected 1.$i"
        set rtOK 0
        break
    }
}
if {$rtOK} { puts "PASS: bbox/index round-trip consistent" }

# --- Check 3: base character cells of line 1 do not overlap ----------------
set cells {}
for {set i 0} {$i < $n1} {incr i} {
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

# --- Check 4: simulated mouse drag over the LTR reference -----------------
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

# --- Check 5: the Hebrew verse (line 2) stays laid out right-to-left --------
# Line 2 starts with a strong RTL letter (U+05D5), so its base clusters
# must appear in decreasing x order.  Combining marks share the cell of
# their base and are skipped.  This guards against the NSM fix flipping
# legitimate RTL lines.
set prevX 1000000
set rtlOK 1
set firstX -1
set lastX -1
for {set i 0} {$i < $n2} {incr i} {
    set ch [scan [.t get 2.$i] %c]
    if {[isMark $ch]} continue
    set b [.t bbox 2.$i]
    if {$b eq ""} continue
    lassign $b bx by bw bh
    if {$bw <= 2} continue
    if {$firstX < 0} { set firstX $bx }
    set lastX $bx
    if {$bx > $prevX + 1} {
        fail "rtl: verse index 2.$i is at x=$bx, right of previous base at x=$prevX (line 2 starts with strong RTL text and must be laid out right-to-left)"
        set rtlOK 0
        break
    }
    set prevX $bx
}
if {$rtlOK && $firstX >= 0 && $lastX > $firstX} {
    fail "rtl: verse laid out left-to-right (first base at x=$firstX, last at x=$lastX)"
    set rtlOK 0
}
if {$rtlOK} { puts "PASS: Hebrew verse laid out right-to-left (strong RTL start respected)" }

# --- Summary ----------------------------------------------------------------
if {$::failures} {
    puts "RESULT: FAIL ($::failures)"
    exit 1
}
puts "RESULT: PASS"
exit 0