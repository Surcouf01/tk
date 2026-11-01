# Mixed-bidi layout stability: on lines mixing LTR and RTL runs (and lines
# where a different-font tag creates several bidi units), the bbox x
# positions of the characters must NOT change when a tag (here "sel")
# splits the line into chunks.  This targets the remaining mixed-bidi
# issues of the dancing_bidi branch.
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}

font create F -family "DejaVu Sans" -size 14
font create FB -family "DejaVu Sans" -size 14 -weight bold
pack [text .t -font F -wrap none]

set failures 0
proc fail {msg} {
    global failures
    incr failures
    puts "FAIL: $msg"
}

# ---------------------------------------------------------------- Line 1 --
# Mixed LTR/RTL, single font. LTR paragraph.
.t delete 1.0 end
.t insert end "hello עולם world טקסט 123"
update
set n1 [.t count -chars 1.0 1.end]

proc probe {w tagname} {
    set n [$w count -chars 1.0 1.end]
    set r {}
    for {set i 0} {$i < $n} {incr i} {
        set b [$w bbox 1.$i]
        lappend r [expr {$b eq "" ? -1 : [lindex $b 0]}]
    }
    return $r
}

set base1 [probe .t sel]
foreach {a b} {1.3 1.8  1.6 1.9  1.2 1.14  1.4 1.end} {
    .t tag remove sel 1.0 end
    .t tag add sel $a $b
    update
    set p [probe .t sel]
    set diff {}
    for {set i 0} {$i < $n1} {incr i} {
        if {[lindex $base1 $i] ne [lindex $p $i]} {lappend diff $i}
    }
    if {[llength $diff] > 0} {
        fail "line 1 (mixed, single font): sel $a..$b moved x of indices: $diff"
        puts "  x before: $base1"
        puts "  x after : $p"
    }
}
if {!$failures} {
    puts "PASS: line 1 (mixed LTR/RTL, single font): layout stable under sel splits"
}

# ---------------------------------------------------------------- Line 2 --
# RTL paragraph where a different-font tag creates separate bidi units:
# sel/bold splits do not change the glyph positions.
.t delete 1.0 end
.t insert end "שלום "
.t insert end "ABC" bold
.t insert end " עולם"
.t tag configure bold -font FB
update
set n2 [.t count -chars 1.0 1.end]

set base2 [probe .t sel]
# Expected visual order, RTL paragraph: first Hebrew run rightmost,
# "ABC" (LTR unit) to its left, trailing Hebrew run leftmost.
lassign [.t bbox 1.0] xh0
set iABC [.t search -count cABC "ABC" 1.0]
lassign [.t bbox $iABC] xabc
set iend [expr {$n2 - 1}]
lassign [.t bbox 1.$iend] xh9
if {$xh0 eq "" || $xabc eq "" || $xh9 eq ""} {
    fail "line 2: empty bbox (xh0=$xh0 xabc=$xabc xh9=$xh9)"
} elseif {!($xh0 > $xabc && $xabc > $xh9)} {
    fail "line 2: visual order wrong: firstHebrew x=$xh0, ABC x=$xabc, lastHebrew x=$xh9 (expected firstHebrew > ABC > lastHebrew)"
}

foreach {a b} {1.2 1.4  1.0 1.5  1.3 1.6} {
    .t tag remove sel 1.0 end
    .t tag add sel $a $b
    update
    set p [probe .t sel]
    set diff {}
    for {set i 0} {$i < $n2} {incr i} {
        if {[lindex $base2 $i] ne [lindex $p $i]} {lappend diff $i}
    }
    if {[llength $diff] > 0} {
        fail "line 2 (bold unit inside RTL): sel $a..$b moved x of indices: $diff"
        puts "  x before: $base2"
        puts "  x after : $p"
    }
}
if {!$failures} {
    puts "PASS: line 2 (bold LTR unit inside RTL paragraph): visual order correct, layout stable under sel splits"
}

# ---------------------------------------------------------------- Round trip --
foreach ln {1} {
    .t tag remove sel 1.0 end
    .t delete 1.0 end
    .t insert end "abc שלום def עולם ghi"
    update
    set n [.t count -chars 1.0 1.end]
    for {set i 0} {$i < $n} {incr i} {
        set bb [.t bbox 1.$i]
        if {$bb eq ""} continue
        lassign $bb bx by bw bh
        set back [.t index @[expr {$bx + $bw/2}],[expr {$by + $bh/2}]]
        scan $back "%d.%d" _ bi
        if {$bi != $i && $bi != $i+1} {
            fail "round-trip: index 1.$i bbox center maps to $back"
            break
        }
    }
}
if {!$failures} {
    puts "PASS: bbox/index round-trip consistent on the mixed line"
}

if {$failures} {
    puts "RESULT: FAIL ($failures)"
    exit 1
}
puts "RESULT: PASS"
exit 0
