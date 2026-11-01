# Diagnostic for the mixed-bidi round-trip failure on dancing_bidi:
# per-index bboxes and an x sweep of the hit-test, on
# "abc שלום def עולם ghi" (LTR paragraph, mixed runs, single font).
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}
pack [text .t -font {{DejaVu Sans} 14} -wrap none]
.t insert end "abc שלום def עולם ghi"
update

set n [.t count -chars 1.0 1.end]
puts "nchars=$n  line='[.t get 1.0 1.end]'"
puts "dlineinfo: [.t dlineinfo 1.0]"
puts "---- per-index bbox (i: x y w h char) ----"
for {set i 0} {$i < $n} {incr i} {
    set b [.t bbox 1.$i]
    set ch [.t get 1.$i]
    if {$b eq ""} {
        puts "  $i: EMPTY  char=[scan $ch %c]"
    } else {
        puts "  $i: $b  char=[scan $ch %c]"
    }
}

lassign [.t dlineinfo 1.0] lx ly lw lh lb
set y [expr {$ly + $lh / 2}]
puts "---- x sweep (x -> index) ----"
for {set x 0} {$x < $lw} {incr x 2} {
    puts "  x=[expr {$lx + $x}] -> [.t index @[expr {$lx + $x}],$y]"
}
exit 0
