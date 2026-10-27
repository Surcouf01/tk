# Cluster boundary snapping: ::tk::TextClosestGap must never return a
# position whose character is a combining mark, i.e. the selection
# boundary must never split a grapheme cluster (base + nikkud/teamim).
# Requires libicu (tk::startOfCluster / tk::endOfCluster).
if {[catch {package require Tk} err]} {
    puts "SKIP: $err"
    exit 0
}
if {[llength [info commands ::tk::startOfCluster]] == 0} {
    puts "SKIP: libicu not found, tk::startOfCluster unavailable"
    exit 0
}
pack [text .t -font {{DejaVu Sans} 14}]
.t insert end "בְּרֵאשִׁית בָּרָא אֱלֹהִים"
update
lassign [.t dlineinfo 1.0] x y w h baseline
set fail 0
for {set px 0} {$px < $w} {incr px} {
    set gap [::tk::TextClosestGap .t [expr {$x + $px}] [expr {$y + $h/2}]]
    set ch [string index [.t get $gap] 0]
    if {$ch eq ""} continue
    scan $ch %c cp
    if {$cp >= 0x0591 && $cp <= 0x05C7} {
        puts "FAIL: gap at px=$px returned index $gap whose char is U+[format %04X $cp] (inside a cluster)"
        set fail 1
        break
    }
}
if {!$fail} {
    puts "PASS: selection boundaries never split a grapheme cluster"
}
exit $fail
