# bididemo-click.tcl -- Reproduce Kevin's remaining bug 1 exactly as
# reported: "run the bidi text widget demo and click the mouse at the
# start of the Hebrew [block] on the right hand side" -- the caret is
# said to be inset by one glyph at the start of the RTL paragraph and
# cannot be placed at the right margin.
#
# This script rebuilds the text widget of library/demos/biditext.tcl
# (same -wrap word -width 70 -height 15 -undo 1 -font TkDefaultFont and
# the same three paragraphs), then:
#
#  1. locates the display lines of the Hebrew paragraph,
#  2. for the first display lines, simulates mouse clicks (Button-1) at
#     the right-hand edge of the text and further right, in the empty
#     margin area; the resulting insert index must be the START of the
#     display line (the logically first character, which is the
#     rightmost one),
#  3. checks where the caret is actually drawn when the insert mark is
#     at the start of the Hebrew paragraph: at the right margin, not
#     inset by one glyph and not on top of the first character.

fconfigure stdout -buffering none
package require Tk
wm withdraw .
set ::tkBidiLog ""

text .t -wrap word -width 70 -height 15 -undo 1 -font TkDefaultFont \
    -bd 0 -highlightthickness 0 -padx 0 -pady 0
pack .t

# --- same content as the demo ----------------------------------------------
.t insert end "1. Left-to-right (LTR) paragraph:\n"
.t insert end "   English text flows from left to right. This is the default direction for many languages.\n\n"

set rtl_text {זהו טקסט לדוגמה בעברית שנועד לבדוק תצוגה של פסקאות מימין לשמאל. הטקסט כולל משפטים באורך שונה כדי לאפשר בדיקה של גלישת שורות, מיקום הסמן, בחירת טקסט ויישור נכון בתוך רכיבי ממשק משתמש שונים. כאשר המערכת תומכת בכיוון כתיבה תקין, כל השורות אמורות להתחיל בצד ימין ולהמשיך באופן טבעי לאורך הפסקה.

    פסקה נוספת בעברית יכולה לסייע בבדיקת מעברי שורה מורכבים יותר. לעיתים מופיעות בעיות כאשר הסמן נע בין תווים או כאשר טקסט נמחק ונוסף מחדש. חשוב לבדוק גם תצוגה תחת גופנים שונים ובגדלים שונים כדי לוודא שהתנהגות המערכת נשארת עקבית.}

.t insert end "2. Right-to-left (RTL) paragraph:\n"
.t insert end $rtl_text
.t insert end "\n\n"
.t insert end "3. Mixed bidirectional paragraph:\n"
.t insert end "   The price is 100 USD (LTR) while السعر هو ١٠٠ دولار (RTL).\n"
.t insert end "   Numbers and punctuation are handled according to Unicode bidi rules."

.t mark set insert 1.0
update idletasks
after 300
update

# --- helpers ----------------------------------------------------------------

# Start index of the display line containing $idx.
proc dlineStart {w idx} {
    set info [$w dlineinfo $idx]
    if {$info eq ""} { return "" }
    set y [lindex $info 1]
    set cur $idx
    while {1} {
        set prev [$w index "$cur - 1 chars"]
        if {$prev eq $cur} { break }
        set pi [$w dlineinfo $prev]
        if {$pi eq "" || [lindex $pi 1] != $y} { break }
        set cur $prev
    }
    return $cur
}

# Start index of the next display line after the line starting at $start.
proc dlineNext {w start} {
    set info [$w dlineinfo $start]
    if {$info eq ""} { return "" }
    set y [lindex $info 1]
    set cur [$w index "$start + 1 chars"]
    while {1} {
        set info [$w dlineinfo $cur]
        if {$info eq ""} { return "" }
        if {[lindex $info 1] != $y} { return $cur }
        set cur [$w index "$cur + 1 chars"]
        if {[$w compare $cur == end]} { return "" }
    }
}

proc clickAt {w x y} {
    set ::tkBidiLog ""
    event generate $w <Button-1> -x $x -y $y
    event generate $w <ButtonRelease-1> -x $x -y $y
    update idletasks
    after 100
    update
    return [$w index insert]
}

proc lastCaretX {} {
    set cx ""
    foreach l [split $::tkBidiLog "\n"] {
        if {[regexp {BD2 caret x=(-?[0-9]+)} $l -> v]} { set cx $v }
    }
    return $cx
}

# --- locate the Hebrew paragraph display lines ------------------------------

set heIdx [.t search -regexp {[\u05d0-\u05ea]} 1.0]
if {$heIdx eq ""} {
    puts "RESULT: FAIL (no Hebrew text found)"
    exit 1
}
set start0 [dlineStart .t $heIdx]
puts "HEBREW first char index: $start0 (found via $heIdx)"

# collect the first three display lines of the Hebrew paragraph
set starts {}
set s $start0
for {set k 0} {$k < 3} {incr k} {
    if {$s eq ""} { break }
    lappend starts $s
    set s [dlineNext .t $s]
}

# --- dump geometry + click tests --------------------------------------------
set failures 0
foreach s $starts {
    set info [.t dlineinfo $s]
    lassign $info dx dy dw dh
    set rightX [expr {$dx + $dw}]
    set midY [expr {$dy + $dh / 2}]
    set bb [.t bbox $s]
    lassign $bb gx gy gw gh
    puts "DLSTART $s dline=$info rightX=$rightX firstcharbbox=$bb"

    # caret placed at the start of this display line: where is it drawn?
    .t mark set insert $s
    .t see insert
    set ::tkBidiLog ""
    update idletasks
    after 200
    update
    set caretX [lastCaretX]
    puts "CARET at $s caretX=$caretX rightX=$rightX firstcharRight=[expr {$gx + $gw}]"
    if {$caretX eq ""} {
        puts "NOTE: no BD2 caret log for $s (line not redrawn?)"
    } else {
        if {$rightX - $caretX > 3} {
            puts "FAIL: caret at $s drawn at x=$caretX, inset by [expr {$rightX - $caretX}] px from the right margin ($rightX)"
            incr failures
        }
        if {$caretX < $gx + $gw - 1} {
            puts "FAIL: caret at $s (x=$caretX) overlaps the first character cell (right edge [expr {$gx + $gw}])"
            incr failures
        }
    }

    # click at the right edge and further right, in the empty margin area
    foreach dx {0 2 5 10} {
        set x [expr {$rightX + $dx}]
        if {$x > [winfo width .t]} { continue }
        .t mark set insert 1.0
        update idletasks
        set got [clickAt .t $x $midY]
        puts "CLICK dx=+$dx x=$x -> insert=$got (expected $s)"
        if {$dx > 0 && $got ne $s} {
            puts "FAIL: click at right margin +$dx of display line $s gave $got instead of $s"
            incr failures
        }
        if {$dx == 0 && $got ne $s} {
            puts "NOTE: click exactly at the right edge gave $got (expected $s); not counted as failure (glyph boundary)"
        }
    }
}

if {$failures > 0} {
    puts "RESULT: FAIL ($failures)"
} else {
    puts "RESULT: PASS"
}
exit 0
