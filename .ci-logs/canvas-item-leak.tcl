# canvas-item-leak.tcl --
#
# Demonstrator for Tk ticket [f476bada5b445d6b465a] (duplicate
# [92d0a16102]): deleting canvas items leaks ~81 bytes per item in
# Tk 9.0.2-9.0.4 (two Tcl_Obj reference leaks: Tk_DeleteOutline()
# misses outline->offsetObj, DeleteText() misses textPtr->widthObj).
# Fixed on trunk and core-9-0-branch; this script reproduces the leak
# on a vulnerable build and passes on a fixed one.
#
# Usage (headless): xvfb-run -a wish canvas-item-leak.tcl
# Measures VmRSS from /proc/self/status (Linux). Verdict per phase:
#   PASS  < 12 bytes/item
#   LEAK  > 24 bytes/item
#   MARGINAL in between (rerun)
# Exit status 1 if any phase LEAKs.

package require Tk

set ::leak 0

proc rss {} {
    if {[catch {open /proc/self/status r} fp]} { return -1 }
    set d [read $fp]
    close $fp
    if {[regexp {VmRSS:\s+(\d+) kB} $d -> kb]} { return $kb }
    return -1
}

# phase label createScript n warmup iters
proc phase {label createScript n warmup iters} {
    for {set i 0} {$i < $warmup} {incr i} {
        .c delete all
        for {set k 0} {$k < $n} {incr k} {
            eval $createScript
        }
        update
    }
    set base [rss]
    for {set i 0} {$i < $iters} {incr i} {
        .c delete all
        for {set k 0} {$k < $n} {incr k} {
            eval $createScript
        }
        update
    }
    set end [rss]
    if {$base < 0 || $end < 0} {
        puts "RESULT $label: UNKNOWN (no /proc/self/status)"
        return
    }
    set perItem [expr {double($end - $base) * 1024.0 / ($iters * $n)}]
    set verdict PASS
    if {$perItem > 24.0} {
        set verdict LEAK
        incr ::leak
    } elseif {$perItem > 12.0} {
        set verdict MARGINAL
    }
    puts [format "RESULT %-10s %-8s %8.1f bytes/item   (RSS %d -> %d kB, %d items)" \
        $label $verdict $perItem $base $end [expr {$iters * $n}]]
    flush stdout
}

canvas .c -width 480 -height 384 -highlightthickness 0
pack .c
update

puts "Tk [package present Tk], PID [pid], start RSS [rss] kB"

# 1. Rectangle items (outline-based: leaks offsetObj on vulnerable builds)
phase rect {.c create rectangle 10 10 100 100 -outline #5cb87f} 112 300 1500

# 2. Text items (leak widthObj on vulnerable builds)
phase text {.c create text 50 50 -text "x 42" -font {Helvetica -12} -fill #5cb87f} 112 300 1500

# 3. Control: same items reused, coords only -> must stay flat
.c delete all
set ids {}
for {set k 0} {$k < 112} {incr k} {
    lappend ids [.c create rectangle 10 10 100 100 -outline #5cb87f]
    lappend ids [.c create text 50 50 -text "x" -font {Helvetica -12}]
}
update
set base [rss]
for {set i 0} {$i < 1500} {incr i} {
    foreach id $ids { .c coords $id [expr {10 + $i % 50}] 10 [expr {100 + $i % 50}] 100 }
    update
}
set end [rss]
puts [format "RESULT %-10s %-8s (RSS %d -> %d kB)" control \
    [expr {$end - $base < 2048 ? "PASS" : "LEAK"}] $base $end]

# 4. delete all on an EMPTY canvas: must not grow either
.c delete all
update
set base [rss]
for {set i 0} {$i < 3000} {incr i} { .c delete all; update }
set end [rss]
puts [format "RESULT %-10s %-8s (RSS %d -> %d kB)" empty-del \
    [expr {$end - $base < 512 ? "PASS" : "LEAK"}] $base $end]

if {$::leak > 0} {
    puts "VERDICT: LEAK ($::leak phase(s)) - see per-item figures above"
    exit 1
}
puts "VERDICT: no item-deletion leak detected"
exit 0
