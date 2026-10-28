/*
 * tkTextDisp.c --
 *
 *	This module provides facilities to display text widgets. It is the
 *	only place where information is kept about the screen layout of text
 *	widgets. (Well, strictly, each TkTextLine and B-tree node caches its
 *	last observed pixel height, but that information originates here).
 *
 * Copyright © 1992-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkText.h"
#include "tk3d.h"

#ifdef _WIN32
#include "tkWinInt.h"
#elif defined(MAC_OSX_TK)
#include "tkMacOSXInt.h"
#else
#include "tkUnixInt.h"
#endif

/*
 * "Calculations of line pixel heights and the size of the vertical
 * scrollbar."
 *
 * Given that tag, font and elide changes can happen to large numbers of
 * diverse chunks in a text widget containing megabytes of text, it is not
 * possible to recalculate all affected height information immediately any
 * such change takes place and maintain a responsive user-experience. Yet, for
 * an accurate vertical scrollbar to be drawn, we must know the total number
 * of vertical pixels shown on display versus the number available to be
 * displayed.
 *
 * The way the text widget solves this problem is by maintaining cached line
 * pixel heights (in the BTree for each logical line), and having asynchronous
 * timer callbacks (i) to iterate through the logical lines recalculating
 * their heights, and (ii) to recalculate the vertical scrollbar's position
 * and size.
 *
 * Typically this works well but there are some situations where the overall
 * functional design of this file causes some problems. These problems can
 * only arise because the calculations used to display lines on screen are not
 * connected to those in the iterating-line- recalculation-process.
 *
 * The reason for this disconnect is that the display calculations operate in
 * display lines, and the iteration and cache operates in logical lines.
 * Given that the display calculations both need not contain complete logical
 * lines (at top or bottom of display), and that they do not actually keep
 * track of logical lines (for simplicity of code and historical design), this
 * means a line may be known and drawn with a different pixel height to that
 * which is cached in the BTree, and this might cause some temporary
 * undesirable mismatch between display and the vertical scrollbar.
 *
 * All such mismatches should be temporary, however, since the asynchronous
 * height calculations will always catch up eventually.
 *
 * For further details see the comments before and within the following
 * functions below: LayoutDLine, AsyncUpdateLineMetrics, GetYView,
 * GetYPixelCount, TkTextUpdateOneLine, TkTextUpdateLineMetrics.
 *
 * For details of the way in which the BTree keeps track of pixel heights, see
 * tkTextBTree.c. Basically the BTree maintains two pieces of information: the
 * logical line indices and the pixel height cache.
 */

/*
 * TK_LAYOUT_WITH_BASE_CHUNKS:
 *
 *	With this macro set, collect all char chunks that have no holes
 *	between them, that are on the same line and use the same font and font
 *	size. Allocate the chars of all these chunks, the so-called "stretch",
 *	in a DString in the first chunk, the so-called "base chunk". Use the
 *	base chunk string for measuring and drawing, so that these actions are
 *	always performed with maximum context.
 *
 *	This is necessary for text rendering engines that provide ligatures
 *	and sub-pixel layout, like CoreText on macOS. If we don't do this, the
 *	measuring will change all the time, leading to an ugly "tremble and
 *	shiver" effect. This is because of the continuous splitting and
 *	re-merging of chunks that goes on in a text widget, when the cursor or
 *	the selection move.
 *
 * Side effects:
 *
 *	Memory management changes. Instead of attaching the character data to
 *	the clientData structures of the char chunks, an additional DString is
 *	used. The collection process will even lead to resizing this DString
 *	for large stretches (> TCL_DSTRING_STATIC_SIZE == 200). We could
 *	reduce the overall memory footprint by copying the result to a plain
 *	char array after the line breaking process, but that would complicate
 *	the code and make performance even worse speedwise. See also TODOs.
 *
 * TODOs:
 *
 *    -	Move the character collection process from the LayoutProc into
 *	LayoutDLine(), so that the collection can be done before actual
 *	layout. In this way measuring can look at the following text, too,
 *	right from the beginning. Memory handling can also be improved with
 *	this. Problem: We don't easily know which chunks are adjacent until
 *	all the other chunks have calculated their width. Apparently marks
 *	would return width==0. A separate char collection loop would have to
 *	know these things.
 *
 *    -	Use a new context parameter to pass the context from LayoutDLine() to
 *	the LayoutProc instead of using a global variable like now. Not
 *	pressing until the previous point gets implemented.
 */

/*
 * The following structure describes how to display a range of characters.
 * The information is generated by scanning all of the tags associated with
 * the characters and combining that with default information for the overall
 * widget. These structures form the hash keys for dInfoPtr->styleTable.
 */

typedef struct StyleValues {
    Tk_3DBorder border;		/* Used for drawing background under text.
				 * NULL means use widget background. */
    int borderWidth;		/* Width of 3-D border for background. */
    int relief;			/* 3-D relief for background. */
    Pixmap bgStipple;		/* Stipple bitmap for background. None means
				 * draw solid. */
    XColor *fgColor;		/* Foreground color for text. */
    Tk_Font tkfont;		/* Font for displaying text. */
    Pixmap fgStipple;		/* Stipple bitmap for text and other
				 * foreground stuff. None means draw solid.*/
    int justify;		/* Justification style for text. */
    int lMargin1;		/* Left margin, in pixels, for first display
				 * line of each text line. */
    int lMargin2;		/* Left margin, in pixels, for second and
				 * later display lines of each text line. */
    Tk_3DBorder lMarginColor;	/* Color of left margins (1 and 2). */
    int offset;			/* Offset in pixels of baseline, relative to
				 * baseline of line. */
    int overstrike;		/* Non-zero means draw overstrike through
				 * text. */
    XColor *overstrikeColor;	/* Foreground color for overstrike through
				 * text. */
    int rMargin;		/* Right margin, in pixels. */
    Tk_3DBorder rMarginColor;	/* Color of right margin. */
    int spacing1;		/* Spacing above first dline in text line. */
    int spacing2;		/* Spacing between lines of dline. */
    int spacing3;		/* Spacing below last dline in text line. */
    TkTextTabArray *tabArrayPtr;/* Locations and types of tab stops (may be
				 * NULL). */
    TkTextTabStyle tabStyle;	/* One of TK_TEXT_TABSTYLE_TABULAR
				 * or TK_TEXT_TABSTYLE_WORDPROCESSOR. */
    int underline;		/* Non-zero means draw underline underneath
				 * text. */
    XColor *underlineColor;	/* Foreground color for underline underneath
				 * text. */
    int elide;			/* Zero means draw text, otherwise not. */
    TkWrapMode wrapMode;	/* How to handle wrap-around for this tag.
				 * One of TEXT_WRAPMODE_CHAR,
				 * TEXT_WRAPMODE_NONE or TEXT_WRAPMODE_WORD.*/
    char locale[8];
} StyleValues;

/*
 * The following structure extends the StyleValues structure above with
 * graphics contexts used to actually draw the characters. The entries in
 * dInfoPtr->styleTable point to structures of this type.
 */

typedef struct TextStyle {
    Tcl_Size refCount;		/* Number of times this structure is
				 * referenced in Chunks. */
    GC bgGC;			/* Graphics context for background. None means
				 * use widget background. */
    GC fgGC;			/* Graphics context for foreground. */
    GC ulGC;			/* Graphics context for underline. */
    GC ovGC;			/* Graphics context for overstrike. */
    StyleValues *sValuePtr;	/* Raw information from which GCs were
				 * derived. */
    Tcl_HashEntry *hPtr;	/* Pointer to entry in styleTable. Used to
				 * delete entry. */
} TextStyle;

/*
 * The following macro determines whether two styles have the same background
 * so that, for example, no beveled border should be drawn between them.
 */

#define SAME_BACKGROUND(s1, s2) \
    (((s1)->sValuePtr->border == (s2)->sValuePtr->border) \
	&& ((s1)->sValuePtr->borderWidth == (s2)->sValuePtr->borderWidth) \
	&& ((s1)->sValuePtr->relief == (s2)->sValuePtr->relief) \
	&& ((s1)->sValuePtr->bgStipple == (s2)->sValuePtr->bgStipple))

/*
 * The following macro is used to compare two floating-point numbers to within
 * a certain degree of scale. Direct comparison fails on processors where the
 * processor and memory representations of FP numbers of a particular
 * precision is different (e.g. Intel)
 */

#define FP_EQUAL_SCALE(double1, double2, scaleFactor) \
    (fabs((double1)-(double2))*((scaleFactor)+1.0) < 0.3)

/*
 * Macros to make debugging/testing logging a little easier.
 */

#define LOG(toVar,what)							\
    Tcl_SetVar2(textPtr->interp, toVar, NULL, (what),			\
		TCL_GLOBAL_ONLY|TCL_APPEND_VALUE|TCL_LIST_ELEMENT)
#define CLEAR(var)							\
    Tcl_SetVar2(interp, var, NULL, "", TCL_GLOBAL_ONLY)

/*
 * The following structure describes one line of the display, which may be
 * either part or all of one line of the text.
 */

typedef struct DLine {
    TkTextIndex index;		/* Identifies first character in text that is
				 * displayed on this line. */
    Tcl_Size byteCount;		/* Number of bytes accounted for by this
				 * display line, including a trailing space or
				 * newline that isn't actually displayed. */
    int logicalLinesMerged;	/* Number of extra logical lines merged into
				 * this one due to elided newlines. */
    int y;			/* Y-position at which line is supposed to be
				 * drawn (topmost pixel of rectangular area
				 * occupied by line). */
    int oldY;			/* Y-position at which line currently appears
				 * on display. This is used to move lines by
				 * scrolling rather than re-drawing. If
				 * 'flags' have the OLD_Y_INVALID bit set,
				 * then we will never examine this field
				 * (which means line isn't currently visible
				 * on display and must be redrawn). */
    int height;			/* Height of line, in pixels. */
    int baseline;		/* Offset of text baseline from y, in
				 * pixels. */
    int spaceAbove;		/* How much extra space was added to the top
				 * of the line because of spacing options.
				 * This is included in height and baseline. */
    int spaceBelow;		/* How much extra space was added to the
				 * bottom of the line because of spacing
				 * options. This is included in height. */
    Tk_3DBorder lMarginColor;	/* Background color of the area corresponding
				 * to the left margin of the display line. */
    int lMarginWidth;		/* Pixel width of the area corresponding to
				 * the left margin. */
    Tk_3DBorder rMarginColor;	/* Background color of the area corresponding
				 * to the right margin of the display line. */
    int rMarginWidth;		/* Pixel width of the area corresponding to
				 * the right margin. */
    int length;			/* Total length of line, in pixels. */
    TkTextDispChunk *chunkPtr;	/* Pointer to first chunk in list of all of
				 * those that are displayed on this line of
				 * the screen. */
    struct DLine *nextPtr;	/* Next in list of all display lines for this
				 * window. The list is sorted in order from
				 * top to bottom. Note: the next DLine doesn't
				 * always correspond to the next line of text:
				 * (a) can have multiple DLines for one text
				 * line (wrapping), (b) can have elided newlines,
				 * and (c) can have gaps where DLine's
				 * have been deleted because they're out of
				 * date. */
    int flags;			/* Various flag bits: see below for values. */
} DLine;

/*
 * Flag bits for DLine structures:
 *
 * HAS_3D_BORDER -		Non-zero means that at least one of the chunks
 *				in this line has a 3D border, so it
 *				potentially interacts with 3D borders in
 *				neighboring lines (see DisplayLineBackground).
 * NEW_LAYOUT -			Non-zero means that the line has been
 *				re-layed out since the last time the display
 *				was updated.
 * TOP_LINE -			Non-zero means that this was the top line in
 *				in the window the last time that the window
 *				was laid out. This is important because a line
 *				may be displayed differently if it's at the top
 *				or bottom than if it's in the middle
 *				(e.g. beveled edges aren't displayed for
 *				middle lines if the adjacent line has a
 *				similar background).
 * BOTTOM_LINE -		Non-zero means that this was the bottom line
 *				in the window the last time that the window
 *				was laid out.
 * OLD_Y_INVALID -		The value of oldY in the structure is not
 *				valid or useful and should not be examined.
 *				'oldY' is only useful when the DLine is
 *				currently displayed at a different position
 *				and we wish to re-display it via scrolling, so
 *				this means the DLine needs redrawing.
 */

#define HAS_3D_BORDER	1
#define NEW_LAYOUT	2
#define TOP_LINE	4
#define BOTTOM_LINE	8
#define OLD_Y_INVALID  16

/*
 * Overall display information for a text widget:
 */

typedef struct TextDInfo {
    Tcl_HashTable styleTable;	/* Hash table that maps from StyleValues to
				 * TextStyles for this widget. */
    DLine *dLinePtr;		/* First in list of all display lines for this
				 * widget, in order from top to bottom. */
    int topPixelOffset;		/* Identifies first pixel in top display line
				 * to display in window. */
    int newTopPixelOffset;	/* Desired first pixel in top display line to
				 * display in window. */
    GC copyGC;			/* Graphics context for copying from off-
				 * screen pixmaps onto screen. */
    GC scrollGC;		/* Graphics context for copying from one place
				 * in the window to another (scrolling):
				 * differs from copyGC in that we need to get
				 * GraphicsExpose events. */
    int x;			/* First x-coordinate that may be used for
				 * actually displaying line information.
				 * Leaves space for border, etc. */
    int y;			/* First y-coordinate that may be used for
				 * actually displaying line information.
				 * Leaves space for border, etc. */
    int maxX;			/* First x-coordinate to right of available
				 * space for displaying lines. */
    int maxY;			/* First y-coordinate below available space
				 * for displaying lines. */
    int topOfEof;		/* Top-most pixel (lowest y-value) that has
				 * been drawn in the appropriate fashion for
				 * the portion of the window after the last
				 * line of the text. This field is used to
				 * figure out when to redraw part or all of
				 * the eof field. */

    /*
     * Information used for scrolling:
     */

    int newXPixelOffset;	/* Desired x scroll position, measured as the
				 * number of pixels off-screen to the left for
				 * a line with no left margin. */
    int curXPixelOffset;	/* Actual x scroll position, measured as the
				 * number of pixels off-screen to the left. */
    int maxLength;		/* Length in pixels of longest line that's
				 * visible in window (length may exceed window
				 * size). If there's no wrapping, this will be
				 * zero. */
    double xScrollFirst, xScrollLast;
				/* Most recent values reported to horizontal
				 * scrollbar; used to eliminate unnecessary
				 * reports. */
    double yScrollFirst, yScrollLast;
				/* Most recent values reported to vertical
				 * scrollbar; used to eliminate unnecessary
				 * reports. */

    /*
     * The following information is used to implement scanning:
     */

    int scanMarkXPixel;		/* Pixel index of left edge of the window when
				 * the scan started. */
    int scanMarkX;		/* X-position of mouse at time scan started. */
    int scanTotalYScroll;	/* Total scrolling (in screen pixels) that has
				 * occurred since scanMarkY was set. */
    int scanMarkY;		/* Y-position of mouse at time scan started. */

    /*
     * Miscellaneous information:
     */

    int dLinesInvalidated;	/* This value is set to 1 whenever something
				 * happens that invalidates information in
				 * DLine structures; if a redisplay is in
				 * progress, it will see this and abort the
				 * redisplay. This is needed because, for
				 * example, an embedded window could change
				 * its size when it is first displayed,
				 * invalidating the DLine that is currently
				 * being displayed. If redisplay continues, it
				 * will use freed memory and could dump
				 * core. */
    int flags;			/* Various flag values: see below for
				 * definitions. */
    /*
     * Information used to handle the asynchronous updating of the y-scrollbar
     * and the vertical height calculations:
     */

    int lineMetricUpdateEpoch;	/* Stores a number which is incremented each
				 * time the text widget changes in a
				 * significant way (e.g. resizing or
				 * geometry-influencing tag changes). */
    Tcl_Size currentMetricUpdateLine;/* Stores a counter which is used to iterate
				 * over the logical lines contained in the
				 * widget and update their geometry
				 * calculations, if they are out of date. */
    TkTextIndex metricIndex;	/* If the current metric update line wraps
				 * into very many display lines, then this is
				 * used to keep track of what index we've got
				 * to so far... */
    int metricPixelHeight;	/* ...and this is for the height calculation
				 * so far...*/
    Tcl_Size metricEpoch;		/* ...and this for the epoch of the partial
				 * calculation so it can be cancelled if
				 * things change once more. This field will be
				 * -1 if there is no long-line calculation in
				 * progress, and take a non-negative value if
				 * there is such a calculation in progress. */
    Tcl_Size lastMetricUpdateLine;	/* When the current update line reaches this
				 * line, we are done and should stop the
				 * asychronous callback mechanism. */
    Tcl_TimerToken lineUpdateTimer;
				/* A token pointing to the current line metric
				 * update callback. */
    Tcl_TimerToken scrollbarTimer;
				/* A token pointing to the current scrollbar
				 * update callback. */
} TextDInfo;

/*
 * In TkTextDispChunk structures for character segments, the clientData field
 * points to one of the following structures:
 */

#ifndef TK_LAYOUT_WITH_BASE_CHUNKS

typedef struct CharInfo {
    Tcl_Size numBytes;		/* Number of bytes to display. */
#ifdef TK_LAYOUT_WITH_BASE_CHUNKS
    char *chars;		/* Pointer to UTF characters to display. */
#else
    char chars[TKFLEXARRAY];		/* UTF characters to display.
				 * Allocated as large as necessary. THIS MUST BE THE LAST
				 * FIELD IN THE STRUCTURE. */
#endif
} CharInfo;

#else /* TK_LAYOUT_WITH_BASE_CHUNKS */

typedef struct CharInfo {
    TkTextDispChunk *baseChunkPtr;
    int baseOffset;		/* Starting offset in base chunk baseChars. */
    Tcl_Size numBytes;		/* Number of bytes that belong to this chunk. */
    int isRtl;			/* Non-zero if this chunk is right-to-left. */
    const char *chars;		/* UTF characters to display. Actually points
				 * into the baseChars of the base chunk. Only
				 * valid after FinalizeBaseChunk(). */
} CharInfo;

/*
 * The BaseCharInfo is a CharInfo with some additional data added.
 */

typedef struct BaseCharInfo {
    CharInfo ci;
    Tcl_DString baseChars;	/* Actual characters for the stretch of text
				 * represented by this base chunk. */
    int width;			/* Width in pixels of the whole string, if
				 * known, else -1. Valid during
				 * LayoutDLine(). */
} BaseCharInfo;

/*
 * The base chunk of the stretch being laid out. Layout may run concurrently
 * in several threads, each with its own interpreter and text widgets.
 */

typedef struct {
    TkTextDispChunk *baseCharChunkPtr;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * ChunkIsRtl --
 *
 *	Determine if a string of UTF-8 text has RTL base direction, by
 *	scanning for the first Unicode character with a strong bidi category.
 *	RTL strong categories: R (Arabic Letter excluded), AL, RLE, RLO, RLI.
 *	LTR strong categories: L, LRE, LRO, LRI.
 *	This is the Unicode P2/P3 paragraph-direction algorithm applied at
 *	chunk level, which is sufficient for the single-script chunks that
 *	TkTextCharLayoutProc produces.
 *
 * Results:
 *	true if the chunk is RTL, false if LTR or neutral.
 */

static bool
ChunkIsRtl(const char *chars, Tcl_Size numBytes)
{
    const char *p = chars;
    const char *end = chars + numBytes;

    while (p < end) {
	int ch;
	Tcl_Size chLen = Tcl_UtfToUniChar(p, &ch);

	if (chLen <= 0) {
	    break;
	}

	/*
	 * Strong RTL ranges (Unicode 15).  We check these first, then LTR.
	 * The first strong character determines the chunk direction (P2/P3).
	 *
	 *   U+0590..U+08FF  Hebrew, Arabic, Syriac, NKo, Samaritan, Mandaic,
	 *                   Arabic Extended-A/B and all related supplements.
	 *                   (Note: 0x0700-0x074F Syriac and 0x07C0-0x07FF NKo
	 *                    are already contained in this single range.)
	 *   U+0780..U+07BF  Thaana  (falls inside the range above; listed
	 *                    separately in comments for clarity)
	 *   U+FB1D..U+FEFF  Hebrew/Arabic Presentation Forms A and B
	 *   U+10800..U+10FFF Historical RTL scripts (Phoenician, Aramaic, …)
	 */

	/* Hebrew, Arabic, Syriac, NKo, Thaana, Samaritan and all supplements */
	if (ch >= 0x0590 && ch <= 0x08FF) {
	    return true;
	}
	/* Hebrew and Arabic presentation forms */
	if (ch >= 0xFB1D && ch <= 0xFEFF) {
	    return true;
	}
	/* Historical RTL scripts */
	if (ch >= 0x10800 && ch <= 0x10FFF) {
	    return true;
	}

	/*
	 * Strong LTR: Basic Latin letters, Latin Extended, Greek, Cyrillic,
	 * CJK Unified, Hangul, Hiragana/Katakana.  Anything alphabetic that
	 * is not caught by the RTL ranges above is LTR for our purposes.
	 */
	if ((ch >= 0x0041 && ch <= 0x005A) ||   /* A-Z */
	    (ch >= 0x0061 && ch <= 0x007A) ||   /* a-z */
	    (ch >= 0x00C0 && ch <= 0x02AF) ||   /* Latin Extended */
	    (ch >= 0x0370 && ch <= 0x03FF) ||   /* Greek */
	    (ch >= 0x0400 && ch <= 0x04FF) ||   /* Cyrillic */
	    (ch >= 0x4E00 && ch <= 0x9FFF) ||   /* CJK Unified */
	    (ch >= 0xAC00 && ch <= 0xD7AF) ||   /* Hangul */
	    (ch >= 0x3040 && ch <= 0x30FF)) {   /* Hiragana/Katakana */
	    return false;
	}

	/* Neutral character (digit, punctuation, whitespace) — keep scanning */
	p += chLen;
    }

    return false; /* neutral / unknown: treat as LTR */
}

#endif /* TK_LAYOUT_WITH_BASE_CHUNKS */

/*
 * Flag values for TextDInfo structures:
 *
 * DINFO_OUT_OF_DATE:		Non-zero means that the DLine structures for
 *				this window are partially or completely out of
 *				date and need to be recomputed.
 * REDRAW_PENDING:		Means that a when-idle handler has been
 *				scheduled to update the display.
 * REDRAW_BORDERS:		Means window border or pad area has
 *				potentially been damaged and must be redrawn.
 * REPICK_NEEDED:		1 means that the widget has been modified in a
 *				way that could change the current character (a
 *				different character might be under the mouse
 *				cursor now). Need to recompute the current
 *				character before the next redisplay.
 * OUT_OF_SYNC			1 means that the last <<WidgetViewSync>> event had
 *				value 0, indicating that the widget is out of sync.
 */

#define DINFO_OUT_OF_DATE	1
#define REDRAW_PENDING		2
#define REDRAW_BORDERS		4
#define REPICK_NEEDED		8
#define OUT_OF_SYNC		16
/*
 * Action values for FreeDLines:
 *
 * DLINE_FREE:		Free the lines, but no need to unlink them from the
 *			current list of actual display lines.
 * DLINE_UNLINK:	Free and unlink from current display.
 * DLINE_FREE_TEMP:	Free, but don't unlink, and also don't set
 *			'dLinesInvalidated'.
 */

#define DLINE_FREE	  0
#define DLINE_UNLINK	  1
#define DLINE_FREE_TEMP	  2

/*
 * The following counters keep statistics about redisplay that can be checked
 * to see how clever this code is at reducing redisplays.
 */

static int numRedisplays;	/* Number of calls to DisplayText. */
static int linesRedrawn;	/* Number of calls to DisplayDLine. */
static int numCopies;		/* Number of calls to XCopyArea to copy part
				 * of the screen. */
static int lineHeightsRecalculated;
				/* Number of line layouts purely for height
				 * calculation purposes.*/
/*
 * Forward declarations for functions defined later in this file:
 */

static void		AdjustForTab(TkText *textPtr,
			    TkTextTabArray *tabArrayPtr, int index,
			    TkTextDispChunk *chunkPtr);
static void		CharBboxProc(TkText *textPtr,
			    TkTextDispChunk *chunkPtr, Tcl_Size index, int y,
			    int lineHeight, int baseline, int *xPtr,
			    int *yPtr, int *widthPtr, int *heightPtr);
static Tcl_Size		CharChunkMeasureChars(TkTextDispChunk *chunkPtr,
			    const char *chars, Tcl_Size charsLen,
			    Tcl_Size start, Tcl_Size end, int startX, int maxX,
			    int flags, int *nextX);
static void		CharDisplayProc(TkText *textPtr,
			    TkTextDispChunk *chunkPtr, int x, int y,
			    int height, int baseline, Display *display,
			    Drawable dst, int screenY);
static Tcl_Size	CharMeasureProc(TkTextDispChunk *chunkPtr, int x);
static void		CharUndisplayProc(TkText *textPtr,
			    TkTextDispChunk *chunkPtr);
#ifdef TK_LAYOUT_WITH_BASE_CHUNKS
static void		FinalizeBaseChunk(TkTextDispChunk *additionalChunkPtr);
static void		FreeBaseChunk(TkTextDispChunk *baseChunkPtr);
static bool		IsSameFGStyle(TextStyle *style1, TextStyle *style2);
static void		RemoveFromBaseChunk(TkTextDispChunk *chunkPtr);
static bool	      ChunkIsRtl(const char *chars, Tcl_Size numBytes);

#endif
/*
 * Definitions of elided procs. Compiler can't inline these since we use
 * pointers to these functions. ElideDisplayProc and ElideUndisplayProc are
 * special-cased for speed, as potentially many elided DLine chunks if large,
 * tag toggle-filled elided region.
 */
static void		ElideBboxProc(TkText *textPtr,
			    TkTextDispChunk *chunkPtr, Tcl_Size index, int y,
			    int lineHeight, int baseline, int *xPtr,
			    int *yPtr, int *widthPtr, int *heightPtr);
static Tcl_Size	ElideMeasureProc(TkTextDispChunk *chunkPtr, int x);
static void		DisplayDLine(TkText *textPtr, DLine *dlPtr,
			    DLine *prevPtr, Pixmap pixmap);
static void		DisplayLineBackground(TkText *textPtr, DLine *dlPtr,
			    DLine *prevPtr, Pixmap pixmap);
static void		DisplayText(void *clientData);
static DLine *		FindDLine(TkText *textPtr, DLine *dlPtr,
			    const TkTextIndex *indexPtr);
static void		FreeDLines(TkText *textPtr, DLine *firstPtr,
			    DLine *lastPtr, int action);
static void		FreeStyle(TkText *textPtr, TextStyle *stylePtr);
static TextStyle *	GetStyle(const TkText *textPtr, const TkTextIndex *indexPtr);
static void		GetXView(Tcl_Interp *interp, const TkText *textPtr,
			    int report);
static void		GetYView(Tcl_Interp *interp, TkText *textPtr,
			    int report);
static int		GetYPixelCount(TkText *textPtr, DLine *dlPtr);
static DLine *		LayoutDLine(TkText *textPtr,
			    const TkTextIndex *indexPtr);
static Tcl_Size	MeasureChars(Tk_Font tkfont, const char *source,
			    Tcl_Size maxBytes, Tcl_Size rangeStart, Tcl_Size rangeLength,
			    int startX, int maxX, int flags, int *nextXPtr);
static void		MeasureUp(TkText *textPtr,
			    const TkTextIndex *srcPtr, int distance,
			    TkTextIndex *dstPtr, int *overlap);
static int		NextTabStop(Tk_Font tkfont, int x, int tabOrigin);
static void		UpdateDisplayInfo(TkText *textPtr);
static void		YScrollByLines(TkText *textPtr, int offset);
static void		YScrollByPixels(TkText *textPtr, int offset);
static int		SizeOfTab(TkText *textPtr, TkTextTabStyle tabStyle,
			    TkTextTabArray *tabArrayPtr, int *indexPtr, int x,
			    int maxX);
static void		TextChanged(TkText *textPtr,
			    const TkTextIndex *index1Ptr,
			    const TkTextIndex *index2Ptr);
static void		TextInvalidateRegion(TkText *textPtr, Region region);
static void		TextRedrawTag(TkText *textPtr,
			    TkTextIndex *index1Ptr, TkTextIndex *index2Ptr,
			    TkTextTag *tagPtr, bool withTag);
static void		TextInvalidateLineMetrics(TkText *textPtr,
			    TkTextLine *linePtr, Tcl_Size lineCount, TkTextInvalidateAction action);
static int		CalculateDisplayLineHeight(TkText *textPtr,
			    const TkTextIndex *indexPtr, int *byteCountPtr,
			    int *mergedLinePtr);
static void		DlineIndexOfX(TkText *textPtr,
			    DLine *dlPtr, int x, TkTextIndex *indexPtr);
static int		DlineXOfIndex(TkText *textPtr,
			    DLine *dlPtr, Tcl_Size byteIndex);
static int		TextGetScrollInfoObj(Tcl_Interp *interp,
			    TkText *textPtr, Tcl_Size objc,
			    Tcl_Obj *const objv[], double *dblPtr,
			    int *intPtr);
static void		AsyncUpdateLineMetrics(void *clientData);
static void		GenerateWidgetViewSyncEvent(TkText *textPtr, Bool InSync);
static void		AsyncUpdateYScrollbar(void *clientData);
static bool		IsStartOfNotMergedLine(const TkText *textPtr,
			    const TkTextIndex *indexPtr);

/*
 * Result values returned by TextGetScrollInfoObj:
 */

#define TKTEXT_SCROLL_MOVETO	1
#define TKTEXT_SCROLL_PAGES	2
#define TKTEXT_SCROLL_UNITS	3
#define TKTEXT_SCROLL_ERROR	4
#define TKTEXT_SCROLL_PIXELS	5

/*
 *----------------------------------------------------------------------
 *
 * TkTextCreateDInfo --
 *
 *	This function is called when a new text widget is created. Its job is
 *	to set up display-related information for the widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A TextDInfo data structure is allocated and initialized and attached
 *	to textPtr.
 *
 *----------------------------------------------------------------------
 */

void
TkTextCreateDInfo(
    TkText *textPtr)		/* Overall information for text widget. */
{
    TextDInfo *dInfoPtr;
    XGCValues gcValues;

    dInfoPtr = (TextDInfo *)Tcl_Alloc(sizeof(TextDInfo));
    Tcl_InitHashTable(&dInfoPtr->styleTable, sizeof(StyleValues)/sizeof(int));
    dInfoPtr->dLinePtr = NULL;
    dInfoPtr->copyGC = NULL;
    gcValues.graphics_exposures = True;
    dInfoPtr->scrollGC = Tk_GetGC(textPtr->tkwin, GCGraphicsExposures,
	    &gcValues);
    dInfoPtr->topOfEof = 0;
    dInfoPtr->newXPixelOffset = 0;
    dInfoPtr->curXPixelOffset = 0;
    dInfoPtr->maxLength = 0;
    dInfoPtr->xScrollFirst = -1;
    dInfoPtr->xScrollLast = -1;
    dInfoPtr->yScrollFirst = -1;
    dInfoPtr->yScrollLast = -1;
    dInfoPtr->scanMarkXPixel = 0;
    dInfoPtr->scanMarkX = 0;
    dInfoPtr->scanTotalYScroll = 0;
    dInfoPtr->scanMarkY = 0;
    dInfoPtr->dLinesInvalidated = 0;
    dInfoPtr->flags = 0;
    dInfoPtr->topPixelOffset = 0;
    dInfoPtr->newTopPixelOffset = 0;
    dInfoPtr->currentMetricUpdateLine = -1;
    dInfoPtr->lastMetricUpdateLine = -1;
    dInfoPtr->lineMetricUpdateEpoch = 1;
    dInfoPtr->metricEpoch = -1;
    dInfoPtr->metricIndex.textPtr = NULL;
    dInfoPtr->metricIndex.linePtr = NULL;
    dInfoPtr->lineUpdateTimer = NULL;
    dInfoPtr->scrollbarTimer = NULL;

    textPtr->dInfoPtr = dInfoPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFreeDInfo --
 *
 *	This function is called to free up all of the private display
 *	information kept by this file for a text widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots of resources get freed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFreeDInfo(
    TkText *textPtr)		/* Overall information for text widget. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    /*
     * Be careful to free up styleTable *after* freeing up all the DLines, so
     * that the hash table is still intact to free up the style-related
     * information from the lines. Once the lines are all free then styleTable
     * will be empty.
     */

    FreeDLines(textPtr, dInfoPtr->dLinePtr, NULL, DLINE_UNLINK);
    Tcl_DeleteHashTable(&dInfoPtr->styleTable);
    if (dInfoPtr->copyGC != NULL) {
	Tk_FreeGC(textPtr->display, dInfoPtr->copyGC);
    }
    Tk_FreeGC(textPtr->display, dInfoPtr->scrollGC);
    if (dInfoPtr->flags & REDRAW_PENDING) {
	Tcl_CancelIdleCall(DisplayText, textPtr);
    }
    if (dInfoPtr->lineUpdateTimer != NULL) {
	Tcl_DeleteTimerHandler(dInfoPtr->lineUpdateTimer);
	textPtr->refCount--;
	dInfoPtr->lineUpdateTimer = NULL;
    }
    if (dInfoPtr->scrollbarTimer != NULL) {
	Tcl_DeleteTimerHandler(dInfoPtr->scrollbarTimer);
	textPtr->refCount--;
	dInfoPtr->scrollbarTimer = NULL;
    }
    Tcl_Free(dInfoPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetStyle --
 *
 *	This function creates all the information needed to display text at a
 *	particular location.
 *
 * Results:
 *	The return value is a pointer to a TextStyle structure that
 *	corresponds to *sValuePtr.
 *
 * Side effects:
 *	A new entry may be created in the style table for the widget.
 *
 *----------------------------------------------------------------------
 */

static TextStyle *
GetStyle(
    const TkText *textPtr,		/* Overall information about text widget. */
    const TkTextIndex *indexPtr)/* The character in the text for which display
				 * information is wanted. */
{
    TkTextTag **tagPtrs;
    TkTextTag *tagPtr;
    StyleValues styleValues;
    TextStyle *stylePtr;
    Tcl_HashEntry *hPtr;
    Tcl_Size numTags, i;
    int isNew;
    int isSelected;
    XGCValues gcValues;
    unsigned long mask;
    /*
     * The variables below keep track of the highest-priority specification
     * that has occurred for each of the various fields of the StyleValues.
     */
    Tcl_Size borderPrio, borderWidthPrio, reliefPrio, bgStipplePrio;
    Tcl_Size fgPrio, fontPrio, fgStipplePrio;
    Tcl_Size underlinePrio, elidePrio, justifyPrio, offsetPrio;
    Tcl_Size lMargin1Prio, lMargin2Prio, rMarginPrio;
    Tcl_Size lMarginColorPrio, rMarginColorPrio, localePrio;
    Tcl_Size spacing1Prio, spacing2Prio, spacing3Prio;
    Tcl_Size overstrikePrio, tabPrio, tabStylePrio, wrapPrio;

    /*
     * Find out what tags are present for the character, then compute a
     * StyleValues structure corresponding to those tags (scan through all of
     * the tags, saving information for the highest-priority tag).
     */

    tagPtrs = TkBTreeGetTags(indexPtr, textPtr, &numTags);
    borderPrio = borderWidthPrio = reliefPrio = bgStipplePrio = -1;
    fgPrio = fontPrio = fgStipplePrio = -1;
    underlinePrio = elidePrio = justifyPrio = offsetPrio = -1;
    lMargin1Prio = lMargin2Prio = rMarginPrio = -1;
    lMarginColorPrio = rMarginColorPrio = localePrio = -1;
    spacing1Prio = spacing2Prio = spacing3Prio = -1;
    overstrikePrio = tabPrio = tabStylePrio = wrapPrio = -1;
    memset(&styleValues, 0, sizeof(StyleValues));
    styleValues.relief = TK_RELIEF_FLAT;
    styleValues.fgColor = textPtr->fgColor;
    styleValues.underlineColor = textPtr->fgColor;
    styleValues.overstrikeColor = textPtr->fgColor;
    styleValues.tkfont = textPtr->tkfont;
    styleValues.justify = TK_JUSTIFY_LEFT;
    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, textPtr->spacing1Obj, &styleValues.spacing1);
    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, textPtr->spacing2Obj, &styleValues.spacing2);
    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, textPtr->spacing3Obj, &styleValues.spacing3);
    styleValues.tabArrayPtr = textPtr->tabArrayPtr;
    styleValues.tabStyle = textPtr->tabStyle;
    styleValues.wrapMode = textPtr->wrapMode;
    styleValues.elide = 0;
    if (textPtr->locale[0]) {
	memcpy(styleValues.locale, textPtr->locale, sizeof(styleValues.locale));
    }
    isSelected = 0;

    for (i = 0 ; i < numTags; i++) {
	if (textPtr->selTagPtr == tagPtrs[i]) {
	    isSelected = 1;
	    break;
	}
    }

    for (i = 0 ; i < numTags; i++) {
	Tk_3DBorder border;
	XColor *fgColor;

	tagPtr = tagPtrs[i];
	border = tagPtr->border;
	fgColor = tagPtr->fgColor;

	/*
	 * If this is the selection tag, and inactiveSelBorder is NULL (the
	 * default on Windows), then we need to skip it if we don't have the
	 * focus.
	 */

	if ((tagPtr == textPtr->selTagPtr) && !(textPtr->flags & GOT_FOCUS)) {
	    if (textPtr->inactiveSelBorder == NULL
#ifdef MAC_OSX_TK
		    /* Don't show inactive selection in disabled widgets. */
		    || textPtr->state == TK_TEXT_STATE_DISABLED
#endif
	    ) {
		continue;
	    }
	    border = textPtr->inactiveSelBorder;
	}

	if ((tagPtr->selBorder != NULL) && (isSelected)) {
	    border = tagPtr->selBorder;
	}

	if ((tagPtr->selFgColor != NULL) && isSelected) {
	    fgColor = tagPtr->selFgColor;
	}

	if ((border != NULL) && (tagPtr->priority > borderPrio)) {
	    styleValues.border = border;
	    borderPrio = tagPtr->priority;
	}
	if (!TkObjIsEmpty(tagPtr->borderWidthObj)
		&& (tagPtr->priority > borderWidthPrio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->borderWidthObj, &styleValues.borderWidth);
	    borderWidthPrio = tagPtr->priority;
	}
	if ((tagPtr->relief != TK_RELIEF_NULL)
		&& (tagPtr->priority > reliefPrio)) {
	    if (styleValues.border == NULL) {
		styleValues.border = textPtr->border;
	    }
	    styleValues.relief = tagPtr->relief;
	    reliefPrio = tagPtr->priority;
	}
	if ((tagPtr->bgStipple != None)
		&& (tagPtr->priority > bgStipplePrio)) {
	    styleValues.bgStipple = tagPtr->bgStipple;
	    bgStipplePrio = tagPtr->priority;
	}
	if ((fgColor != NULL) && (tagPtr->priority > fgPrio)) {
	    styleValues.fgColor = fgColor;
	    fgPrio = tagPtr->priority;
	}
	if ((tagPtr->tkfont != NULL) && (tagPtr->priority > fontPrio)) {
	    styleValues.tkfont = tagPtr->tkfont;
	    fontPrio = tagPtr->priority;
	}
	if ((tagPtr->fgStipple != None)
		&& (tagPtr->priority > fgStipplePrio)) {
	    styleValues.fgStipple = tagPtr->fgStipple;
	    fgStipplePrio = tagPtr->priority;
	}
	if ((tagPtr->justify != TK_JUSTIFY_NULL)
		&& (tagPtr->priority > justifyPrio)) {
	    styleValues.justify = tagPtr->justify;
	    justifyPrio = tagPtr->priority;
	}
	if ((tagPtr->lMargin1Obj != NULL)
		&& (tagPtr->priority > lMargin1Prio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->lMargin1Obj, &styleValues.lMargin1);
	    lMargin1Prio = tagPtr->priority;
	}
	if ((tagPtr->lMargin2Obj != NULL)
		&& (tagPtr->priority > lMargin2Prio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->lMargin2Obj, &styleValues.lMargin2);
	    lMargin2Prio = tagPtr->priority;
	}
	if ((tagPtr->lMarginColor != NULL)
		&& (tagPtr->priority > lMarginColorPrio)) {
	    styleValues.lMarginColor = tagPtr->lMarginColor;
	    lMarginColorPrio = tagPtr->priority;
	}
	if ((tagPtr->offsetObj != NULL)
		&& (tagPtr->priority > offsetPrio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->offsetObj, &styleValues.offset);
	    offsetPrio = tagPtr->priority;
	}
	if ((tagPtr->overstrike >= 0)
		&& (tagPtr->priority > overstrikePrio)) {
	    styleValues.overstrike = tagPtr->overstrike > 0;
	    overstrikePrio = tagPtr->priority;
	    if (tagPtr->overstrikeColor != NULL) {
		styleValues.overstrikeColor = tagPtr->overstrikeColor;
	    } else if (fgColor != NULL) {
		styleValues.overstrikeColor = fgColor;
	    }
	}
	if ((tagPtr->rMarginObj != NULL)
		&& (tagPtr->priority > rMarginPrio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->rMarginObj, &styleValues.rMargin);
	    rMarginPrio = tagPtr->priority;
	}
	if ((tagPtr->rMarginColor != NULL)
		&& (tagPtr->priority > rMarginColorPrio)) {
	    styleValues.rMarginColor = tagPtr->rMarginColor;
	    rMarginColorPrio = tagPtr->priority;
	}
	if ((tagPtr->spacing1Obj != NULL)
		&& (tagPtr->priority > spacing1Prio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->spacing1Obj, &styleValues.spacing1);
	    spacing1Prio = tagPtr->priority;
	}
	if ((tagPtr->spacing2Obj != NULL)
		&& (tagPtr->priority > spacing2Prio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->spacing2Obj, &styleValues.spacing2);
	    spacing2Prio = tagPtr->priority;
	}
	if ((tagPtr->spacing3Obj != NULL)
		&& (tagPtr->priority > spacing3Prio)) {
	    Tk_GetPixelsFromObj(NULL, textPtr->tkwin, tagPtr->spacing3Obj, &styleValues.spacing3);
	    spacing3Prio = tagPtr->priority;
	}
	if ((tagPtr->tabStringPtr != NULL)
		&& (tagPtr->priority > tabPrio)) {
	    styleValues.tabArrayPtr = tagPtr->tabArrayPtr;
	    tabPrio = tagPtr->priority;
	}
	if (((tagPtr->tabStyle == TK_TEXT_TABSTYLE_TABULAR)
		|| (tagPtr->tabStyle == TK_TEXT_TABSTYLE_WORDPROCESSOR))
		&& (tagPtr->priority > tabStylePrio)) {
	    styleValues.tabStyle = tagPtr->tabStyle;
	    tabStylePrio = tagPtr->priority;
	}
	if ((tagPtr->underline >= 0)
		&& (tagPtr->priority > underlinePrio)) {
	    styleValues.underline = tagPtr->underline > 0;
	    underlinePrio = tagPtr->priority;
	    if (tagPtr->underlineColor != NULL) {
		 styleValues.underlineColor = tagPtr->underlineColor;
	    } else if (fgColor != NULL) {
		 styleValues.underlineColor = fgColor;
	    }
	}
	if ((tagPtr->elide >= 0)
		&& (tagPtr->priority > elidePrio)) {
	    styleValues.elide = tagPtr->elide > 0;
	    elidePrio = tagPtr->priority;
	}
	if (((tagPtr->wrapMode == TEXT_WRAPMODE_CHAR) || (tagPtr->wrapMode == TEXT_WRAPMODE_NONE)
		|| (tagPtr->wrapMode == TEXT_WRAPMODE_WORD)) && (tagPtr->priority > wrapPrio)) {
	    styleValues.wrapMode = tagPtr->wrapMode;
	    wrapPrio = tagPtr->priority;
	}
	if ((tagPtr->locale[0])
		&& (tagPtr->priority > localePrio)) {
	    memcpy(styleValues.locale, tagPtr->locale, sizeof(styleValues.locale));
	    localePrio = tagPtr->priority;
	}
    }
    if (tagPtrs != NULL) {
	Tcl_Free(tagPtrs);
    }

    /*
     * Use an existing style if there's one around that matches.
     */

    hPtr = Tcl_CreateHashEntry(&textPtr->dInfoPtr->styleTable,
	    (char *) &styleValues, &isNew);
    if (!isNew) {
	stylePtr = (TextStyle *)Tcl_GetHashValue(hPtr);
	stylePtr->refCount++;
	return stylePtr;
    }

    /*
     * No existing style matched. Make a new one.
     */

    stylePtr = (TextStyle *)Tcl_Alloc(sizeof(TextStyle));
    stylePtr->refCount = 1;
    if (styleValues.border != NULL) {
	gcValues.foreground = Tk_3DBorderColor(styleValues.border)->pixel;
	mask = GCForeground;
	if (styleValues.bgStipple != None) {
	    gcValues.stipple = styleValues.bgStipple;
	    gcValues.fill_style = FillStippled;
	    mask |= GCStipple|GCFillStyle;
	}
	stylePtr->bgGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    } else {
	stylePtr->bgGC = NULL;
    }
    mask = GCFont;
    gcValues.font = Tk_FontId(styleValues.tkfont);
    mask |= GCForeground;
    gcValues.foreground = styleValues.fgColor->pixel;
    if (styleValues.fgStipple != None) {
	gcValues.stipple = styleValues.fgStipple;
	gcValues.fill_style = FillStippled;
	mask |= GCStipple|GCFillStyle;
    }
    stylePtr->fgGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    mask = GCForeground;
    gcValues.foreground = styleValues.underlineColor->pixel;
    stylePtr->ulGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    gcValues.foreground = styleValues.overstrikeColor->pixel;
    stylePtr->ovGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    stylePtr->sValuePtr = (StyleValues *)
	    Tcl_GetHashKey(&textPtr->dInfoPtr->styleTable, hPtr);
    stylePtr->hPtr = hPtr;
    Tcl_SetHashValue(hPtr, stylePtr);
    return stylePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeStyle --
 *
 *	This function is called when a TextStyle structure is no longer
 *	needed. It decrements the reference count and frees up the space for
 *	the style structure if the reference count is 0.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The storage and other resources associated with the style are freed up
 *	if no-one's still using it.
 *
 *----------------------------------------------------------------------
 */

static void
FreeStyle(
    TkText *textPtr,		/* Information about overall widget. */
    TextStyle *stylePtr)
				/* Information about style to free. */
{
    if (stylePtr->refCount-- <= 1) {
	if (stylePtr->bgGC != NULL) {
	    Tk_FreeGC(textPtr->display, stylePtr->bgGC);
	}
	if (stylePtr->fgGC != NULL) {
	    Tk_FreeGC(textPtr->display, stylePtr->fgGC);
	}
	if (stylePtr->ulGC != NULL) {
	    Tk_FreeGC(textPtr->display, stylePtr->ulGC);
	}
	if (stylePtr->ovGC != NULL) {
	    Tk_FreeGC(textPtr->display, stylePtr->ovGC);
	}
	Tcl_DeleteHashEntry(stylePtr->hPtr);
	Tcl_Free(stylePtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * IsEntirelyElidedLine --
 *
 *	Determine whether the logical line containing the given index, which
 *	must be at the start of the line, is elided from its beginning to its
 *	end, so that it is laid out as a single zero-height display line.
 *
 * Results:
 *	Returns true if the line is entirely elided, in which case the number
 *	of bytes in the line is stored at *bytesPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
IsEntirelyElidedLine(
    TkText *textPtr,		/* Widget record for text widget. */
    const TkTextIndex *indexPtr,/* Index at the start of a logical line. */
    Tcl_Size *bytesPtr)		/* Where to store the number of bytes in the
				 * line. */
{
    TkTextElideInfo info;
    TkTextSegment *segPtr;
    Tcl_Size maxBytes = 0;
    bool elide;

    if (indexPtr->byteIndex != 0) {
	return false;
    }
    elide = TkTextIsElided(textPtr, indexPtr, &info);
    if (elide) {
	for (segPtr = info.segPtr; segPtr != NULL; segPtr = segPtr->nextPtr) {
	    if (segPtr->size > 0) {
		if (elide == 0) {
		    /*
		     * We toggled a tag and the elide state changed to
		     * visible, and we have something of non-zero size.
		     * Therefore we must bail out.
		     */

		    break;
		}
		maxBytes += segPtr->size;

		/*
		 * Reset tag elide priority, since we're on a new character.
		 */

	    } else if ((segPtr->typePtr == &tkTextToggleOffType)
		    || (segPtr->typePtr == &tkTextToggleOnType)) {
		TkTextTag *tagPtr = segPtr->body.toggle.tagPtr;

		/*
		 * The elide state only changes if this tag is either the
		 * current highest priority tag (and is therefore being
		 * toggled off), or it's a new tag with higher priority.
		 */

		if (tagPtr->elide >= 0) {
		    info.tagCnts[tagPtr->priority]++;
		    if (info.tagCnts[tagPtr->priority] & 1) {
			info.tagPtrs[tagPtr->priority] = tagPtr;
		    }
		    if (tagPtr->priority >= info.elidePriority) {
			if (segPtr->typePtr == &tkTextToggleOffType) {
			    /*
			     * If it is being toggled off, and it has an elide
			     * string, it must actually be the current highest
			     * priority tag, so this check is redundant:
			     */

			    if (tagPtr->priority != info.elidePriority) {
				Tcl_Panic("Bad tag priority being toggled off");
			    }

			    /*
			     * Find previous elide tag, if any (if not then
			     * elide will be zero, of course).
			     */

			    elide = 0;
			    while (--info.elidePriority > 0) {
				if (info.tagCnts[info.elidePriority] & 1) {
				    elide = info.tagPtrs[info.elidePriority]
					    ->elide > 0;
				    break;
				}
			    }
			} else {
			    elide = tagPtr->elide > 0;
			    info.elidePriority = tagPtr->priority;
			}
		    }
		}
	    }
	}
    }
    TkTextFreeElideInfo(&info);
    if (elide) {
	*bytesPtr = maxBytes;
    }
    return elide;
}

/*
 *----------------------------------------------------------------------
 *
 * ChunkBidiDir --
 *
	Tri-state version of ChunkIsRtl: returns the strong bidi direction
	of a chunk.  0 = neutral (no strong direction), 1 = LTR, 2 = RTL.
	The strong ranges mirror ChunkIsRtl.
 *
 * Results:
	0, 1 or 2 as described above.
 *
 *----------------------------------------------------------------------
 */

static int
ChunkBidiDir(
    const char *chars,
    Tcl_Size numBytes)
{
    const char *p = chars;
    const char *end = chars + numBytes;

    if (chars == NULL) {
	return 0;
    }
    while (p < end) {
	int ch;
	Tcl_Size chLen = Tcl_UtfToUniChar(p, &ch);

	if (chLen <= 0) {
	    break;
	}
	/* Hebrew, Arabic, Syriac, NKo, Thaana, Samaritan and supplements */
	if (ch >= 0x0590 && ch <= 0x08FF) {
	    return 2;
	}
	/* Hebrew and Arabic presentation forms */
	if (ch >= 0xFB1D && ch <= 0xFEFF) {
	    return 2;
	}
	/* Historical RTL scripts */
	if (ch >= 0x10800 && ch <= 0x10FFF) {
	    return 2;
	}
	/* Any other letter is a strong LTR for our purposes. */
	if (Tcl_UniCharIsAlpha(ch)) {
	    return 1;
	}
	p += chLen;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * BidiReorderChunks --
 *
	Reorder the chunks of a display line per UAX #9, so that the visual
	placement of tag-separated chunks of RTL text matches the reading
	order.  LayoutDLine lays chunks out left-to-right in logical order,
	and each RTL chunk is shaped right-to-left by the platform font
	backend; without this reordering, a tag - the "sel" tag included -
	which splits an RTL line places the chunks left-to-right while each
	of them is drawn right-to-left, which scrambles the line layout and
	makes the mouse selection "dance" while dragging.  This is the
	chunk-granularity port of the approach of the revised text widget
	[e7c85cd941]: resolve the bidi level of each chunk, then apply rule
	L2 by reversing each maximal run of RTL chunks.
 *
	Simplifications (deliberate, for a first fix): the paragraph base
	level is always LTR (0), so RTL paragraphs whose text is entirely in
	one chunk are unaffected (the font backend already shapes the whole
	run); non-character chunks (tabs, images, windows, insertion cursor)
	never move, because their x positions come from tab stops or are
	zero-width; European numbers are treated as neutrals.
 *
 * Results:
	None.
 *
 * Side effects:
	The x positions of the chunks of RTL runs of dlPtr are reassigned.
 *
 *----------------------------------------------------------------------
 */

static void
BidiReorderChunks(
    DLine *dlPtr)				/* Display line to reorder. */
{
    TkTextDispChunk *chunkPtr;
    TkTextDispChunk **chunks;
    int *dir;
    int nChunks = 0, i, j, n;

    for (chunkPtr = dlPtr->chunkPtr; chunkPtr != NULL;
	    	chunkPtr = chunkPtr->nextPtr) {
	nChunks++;
    }
    if (nChunks < 2) {
	return;
    }

    chunks = (TkTextDispChunk **)Tcl_Alloc(nChunks * sizeof(TkTextDispChunk *));
    dir = (int *)Tcl_Alloc(nChunks * sizeof(int));

    i = 0;
    for (chunkPtr = dlPtr->chunkPtr; chunkPtr != NULL;
	    	chunkPtr = chunkPtr->nextPtr) {
	chunks[i] = chunkPtr;
	dir[i] = -1;
	if (chunkPtr->displayProc == CharDisplayProc
			&& chunkPtr->clientData != NULL) {
	    CharInfo *ciPtr = (CharInfo *)chunkPtr->clientData;

	    dir[i] = ChunkBidiDir(ciPtr->chars, ciPtr->numBytes);
	}
	i++;
    }

    /*
     * Process each maximal group of consecutive char chunks.
     */

    for (i = 0; i < nChunks; ) {
	int gStart, gEnd, nLo, nHi, before, after, d;

	if (dir[i] < 0) {
	    i++;
	    continue;
	}
	gStart = i;
	for (gEnd = i; gEnd + 1 < nChunks && dir[gEnd + 1] >= 0; gEnd++) {
	    ;
	}

	/*
	 * Resolve neutrals (UAX #9 N1/N2, simplified): a maximal run of
	 * neutral chunks between two chunks of the same strong direction
	 * takes that direction, otherwise the LTR base level.
	 */

	for (n = gStart; n <= gEnd; n++) {
	    if (dir[n] != 0) {
		continue;
	    }
	    for (nLo = n; nLo <= gEnd && dir[nLo] == 0; nLo++) {
		;
	    }
	    nLo--;
	    for (nHi = nLo; nHi <= gEnd && dir[nHi] == 0; nHi++) {
		;
	    }
	    nHi--;
	    before = (nLo > gStart) ? dir[nLo - 1] : 0;
	    after = (nHi < gEnd) ? dir[nHi + 1] : 0;
	    d = (before != 0 && before == after) ? before : 1;
	    for (j = nLo; j <= nHi; j++) {
		dir[j] = d;
	    }
	    n = nHi;
	}

	/*
	 * Levels and rule L2 (chunk granularity): reverse each maximal run
	 * of RTL chunks, reassigning their x positions in visual order.  The
	 * run keeps its left edge and its total width, so the chunk
	 * positions after the run, and the line length, are unaffected.
	 */

	for (n = gStart; n <= gEnd; n++) {
	    int x, w, runEnd;

	    if (dir[n] != 2) {
		continue;
	    }
	    for (runEnd = n; runEnd + 1 <= gEnd && dir[runEnd + 1] == 2;
	    	    	runEnd++) {
		;
	    }
	    x = chunks[n]->x;
	    w = 0;
	    for (j = runEnd; j >= n; j--) {
		chunks[j]->x = x + w;
		w += chunks[j]->width;
	    }
	    n = runEnd;
	}

	i = gEnd + 1;
    }

    if (getenv("TK_DEBUG_BIDI") != NULL) {
	fprintf(stderr, "BRC after: nChunks=%d\n", nChunks);
	for (i = 0; i < nChunks; i++) {
	    CharInfo *ci = (CharInfo *)chunks[i]->clientData;
	    fprintf(stderr, "  chunk[%d] x=%d w=%d nBytes=%d isRtl=%d baseOff=%d baseX=%d chars=%.*s\n",
			    i, chunks[i]->x, chunks[i]->width, (int)chunks[i]->numBytes,
			    ci ? ci->isRtl : -1,
			    ci ? (int)ci->baseOffset : -1,
			    (ci && ci->baseChunkPtr) ? ci->baseChunkPtr->x : -1,
			    (ci && ci->chars) ? ((ci->numBytes > 12) ? 12 : (int)ci->numBytes) : 0,
			    (ci && ci->chars) ? ci->chars : "");
	}
	fprintf(stderr, "BRC dir: ");
	for (i = 0; i < nChunks; i++) fprintf(stderr, "%d,", dir[i]);
	fprintf(stderr, "\n");
    }
    Tcl_Free(chunks);
    Tcl_Free(dir);
}

/*
 *----------------------------------------------------------------------
 *
 * LayoutDLine --
 *
 *	This function generates a single DLine structure for a display line
 *	whose leftmost character is given by indexPtr.
 *
 * Results:
 *	The return value is a pointer to a DLine structure describing the
 *	display line. All fields are filled in and correct except for y and
 *	nextPtr.
 *
 * Side effects:
 *	Storage is allocated for the new DLine.
 *
 *	See the comments in 'GetYView' for some thoughts on what the side-
 *	effects of this call (or its callers) should be; the synchronisation
 *	of TkTextLine->pixelHeight with the sum of the results of this
 *	function operating on all display lines within each logical line.
 *	Ideally the code should be refactored to ensure the cached pixel
 *	height is never behind what is known when this function is called
 *	elsewhere.
 *
 *	Unfortunately, this function is currently called from many different
 *	places, not just to layout a display line for actual display, but also
 *	simply to calculate some metric or other of one or more display lines
 *	(typically the height). It would be a good idea to do some profiling
 *	of typical text widget usage and the way in which this is called and
 *	see if some optimization could or should be done.
 *
 *----------------------------------------------------------------------
 */

static DLine *
LayoutDLine(
    TkText *textPtr,		/* Overall information about text widget. */
    const TkTextIndex *indexPtr)/* Beginning of display line. May not
				 * necessarily point to a character
				 * segment. */
{
#ifdef TK_LAYOUT_WITH_BASE_CHUNKS
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#endif
    DLine *dlPtr;	/* New display line. */
    TkTextSegment *segPtr;	/* Current segment in text. */
    TkTextDispChunk *lastChunkPtr;
				/* Last chunk allocated so far for line. */
    TkTextDispChunk *chunkPtr;	/* Current chunk. */
    TkTextIndex curIndex;
    TkTextDispChunk *breakChunkPtr;
				/* Chunk containing best word break point, if
				 * any. */
    TkTextIndex breakIndex;	/* Index of first character in
				 * breakChunkPtr. */
    Tcl_Size breakByteOffset;	/* Byte offset of character within
				 * breakChunkPtr just to right of best break
				 * point. */
    int justify;		/* How to justify line: taken from style for
				 * the first character in line. */
    int jIndent;		/* Additional indentation (beyond margins) due
				 * to justification. */
    int rMargin;		/* Right margin width for line. */
    TkWrapMode wrapMode;	/* Wrap mode to use for this line. */
    int x = 0, maxX = 0;	/* Initializations needed only to stop
				 * compiler warnings. */
    int tabIndex;		/* Index of the current tab stop. */
    bool noCharsYet;		/* True means that no characters have been
				 * placed on the line yet. */
    bool paragraphStart;		/* True means that we are on the first
				 * line of a paragraph (used to choose between
				 * lmargin1, lmargin2). */
    bool wholeLine;		/* Non-zero means this display line runs to
				 * the end of the text line. */
    bool gotTab;			/* True means the current chunk contains a
				 * tab. */
    TkTextDispChunk *tabChunkPtr;
				/* Pointer to the chunk containing the
				 * previous tab stop. */
    Tcl_Size maxBytes;		/* Maximum number of bytes to include in this
				 * chunk. */
    TkTextTabArray *tabArrayPtr;/* Tab stops for line; taken from style for
				 * the first character on line. */
    TkTextTabStyle tabStyle;	/* One of TK_TEXT_TABSTYLE_TABULAR
				 * or TK_TEXT_TABSTYLE_WORDPROCESSOR. */
    int tabSize;		/* Number of pixels consumed by current tab
				 * stop. */
    TkTextDispChunk *lastCharChunkPtr;
				/* Pointer to last chunk in display lines with
				 * numBytes > 0. Used to drop 0-sized chunks
				 * from the end of the line. */
    Tcl_Size byteOffset;
    int ascent, descent, code;
	Tcl_Size elidesize;
    bool elide;
    StyleValues *sValuePtr;

    /*
     * Create and initialize a new DLine structure.
     */

    dlPtr = (DLine *)Tcl_Alloc(sizeof(DLine));
    dlPtr->index = *indexPtr;
    dlPtr->byteCount = 0;
    dlPtr->y = 0;
    dlPtr->oldY = 0;		/* Only set to avoid compiler warnings. */
    dlPtr->height = 0;
    dlPtr->baseline = 0;
    dlPtr->chunkPtr = NULL;
    dlPtr->nextPtr = NULL;
    dlPtr->flags = NEW_LAYOUT | OLD_Y_INVALID;
    dlPtr->logicalLinesMerged = 0;
    dlPtr->lMarginColor = NULL;
    dlPtr->lMarginWidth = 0;
    dlPtr->rMarginColor = NULL;
    dlPtr->rMarginWidth = 0;

    /*
     * This is not necessarily totally correct, where we have merged logical
     * lines. Fixing this would require a quite significant overhaul, though,
     * so currently we make do with this.
     */

    paragraphStart = (indexPtr->byteIndex == 0);

    /*
     * Special case entirely elide line as there may be 1000s or more.
     */

    if (IsEntirelyElidedLine(textPtr, indexPtr, &maxBytes)) {
	dlPtr->byteCount = maxBytes;
	dlPtr->spaceAbove = dlPtr->spaceBelow = dlPtr->length = 0;

	/*
	 * Elided state goes from beginning to end of an entire logical
	 * line. This means we can update the line's pixel height, and
	 * bring its pixel calculation up to date.
	 */

	TkBTreeLinePixelEpoch(textPtr, dlPtr->index.linePtr)
		= textPtr->dInfoPtr->lineMetricUpdateEpoch;

	if (TkBTreeLinePixelCount(textPtr,dlPtr->index.linePtr) != 0) {
	    TkBTreeAdjustPixelHeight(textPtr, dlPtr->index.linePtr, 0, 0);
	}
	return dlPtr;
    }

    /*
     * Each iteration of the loop below creates one TkTextDispChunk for the
     * new display line. The line will always have at least one chunk (for the
     * newline character at the end, if there's nothing else available).
     */

    curIndex = *indexPtr;
    lastChunkPtr = NULL;
    chunkPtr = NULL;
    noCharsYet = true;
    elide = 0;
    breakChunkPtr = NULL;
    breakByteOffset = 0;
    justify = TK_JUSTIFY_LEFT;
    tabIndex = -1;
    tabChunkPtr = NULL;
    tabArrayPtr = NULL;
    tabStyle = TK_TEXT_TABSTYLE_TABULAR;
    rMargin = 0;
    wrapMode = TEXT_WRAPMODE_CHAR;
    tabSize = 0;
    lastCharChunkPtr = NULL;

    /*
     * Find the first segment to consider for the line. Can't call
     * TkTextIndexToSeg for this because it won't return a segment with zero
     * size (such as the insertion cursor's mark).
     */

  connectNextLogicalLine:
    byteOffset = curIndex.byteIndex;
    segPtr = curIndex.linePtr->segPtr;
    while ((byteOffset > 0) && (byteOffset >= segPtr->size)) {
	byteOffset -= segPtr->size;
	segPtr = segPtr->nextPtr;

	if (segPtr == NULL) {
	    /*
	     * Two logical lines merged into one display line through eliding
	     * of a newline.
	     */

	    TkTextLine *linePtr = TkBTreeNextLine(NULL, curIndex.linePtr);
	    if (linePtr == NULL) {
		break;
	    }

	    dlPtr->logicalLinesMerged++;
	    curIndex.byteIndex = 0;
	    curIndex.linePtr = linePtr;
	    segPtr = curIndex.linePtr->segPtr;
	}
    }

    while (segPtr != NULL) {
	/*
	 * Every logical line still gets at least one chunk due to
	 * expectations in the rest of the code, but we are able to skip
	 * elided portions of the line quickly.
	 *
	 * If current chunk is elided and last chunk was too, coalesce.
	 *
	 * This also means that each logical line which is entirely elided
	 * still gets laid out into a DLine, but with zero height. This isn't
	 * particularly a problem, but it does seem somewhat unnecessary. We
	 * may wish to redesign the code to remove these zero height DLines in
	 * the future.
	 */

	if (elide && (lastChunkPtr != NULL)
		&& (lastChunkPtr->displayProc == NULL /*ElideDisplayProc*/)) {
	    elidesize = segPtr->size - byteOffset;
	    if (segPtr->size > byteOffset) {
		curIndex.byteIndex += elidesize;
		lastChunkPtr->numBytes += elidesize;
		breakByteOffset = lastChunkPtr->breakIndex
			= lastChunkPtr->numBytes;

		/*
		 * If have we have a tag toggle, there is a chance that
		 * invisibility state changed, so bail out.
		 */
	    } else if ((segPtr->typePtr == &tkTextToggleOffType)
		    || (segPtr->typePtr == &tkTextToggleOnType)) {
		if (segPtr->body.toggle.tagPtr->elide >= 0) {
		    elide = (segPtr->typePtr == &tkTextToggleOffType)
			    ^ (segPtr->body.toggle.tagPtr->elide > 0);
		}
	    }

	    byteOffset = 0;
	    segPtr = segPtr->nextPtr;

	    if (segPtr == NULL) {
		/*
		 * Two logical lines merged into one display line through
		 * eliding of a newline.
		 */

		TkTextLine *linePtr = TkBTreeNextLine(NULL, curIndex.linePtr);

		if (linePtr != NULL) {
		    dlPtr->logicalLinesMerged++;
		    curIndex.byteIndex = 0;
		    curIndex.linePtr = linePtr;
		    goto connectNextLogicalLine;
		}
	    }

	    /*
	     * Code no longer needed, now that we allow logical lines to merge
	     * into a single display line.
	     *
	    if (segPtr == NULL && chunkPtr != NULL) {
		Tcl_Free(chunkPtr);
		chunkPtr = NULL;
	    }
	     */

	    continue;
	}

	if (segPtr->typePtr->layoutProc == NULL) {
	    segPtr = segPtr->nextPtr;
	    byteOffset = 0;
	    continue;
	}
	if (chunkPtr == NULL) {
	    chunkPtr = (TkTextDispChunk *)Tcl_Alloc(sizeof(TkTextDispChunk));
	    chunkPtr->nextPtr = NULL;
	    chunkPtr->clientData = NULL;
	}
	chunkPtr->stylePtr = GetStyle(textPtr, &curIndex);
	elide = chunkPtr->stylePtr->sValuePtr->elide;

	/*
	 * Save style information such as justification and indentation, up
	 * until the first character is encountered, then retain that
	 * information for the rest of the line.
	 */

	if (!elide && noCharsYet) {
	    tabArrayPtr = chunkPtr->stylePtr->sValuePtr->tabArrayPtr;
	    tabStyle = chunkPtr->stylePtr->sValuePtr->tabStyle;
	    justify = chunkPtr->stylePtr->sValuePtr->justify;
	    rMargin = chunkPtr->stylePtr->sValuePtr->rMargin;
	    wrapMode = chunkPtr->stylePtr->sValuePtr->wrapMode;

	    /*
	     * See above - this test may not be entirely correct where we have
	     * partially elided lines (and therefore merged logical lines).
	     * In such a case a byteIndex of zero doesn't necessarily mean the
	     * beginning of a logical line.
	     */

	    if (paragraphStart) {
		/*
		 * Beginning of logical line.
		 */

		x = chunkPtr->stylePtr->sValuePtr->lMargin1;
	    } else {
		/*
		 * Beginning of display line.
		 */

		x = chunkPtr->stylePtr->sValuePtr->lMargin2;
	    }
	    dlPtr->lMarginWidth = x;
	    if (wrapMode == TEXT_WRAPMODE_NONE) {
		maxX = -1;
	    } else {
		maxX = textPtr->dInfoPtr->maxX - textPtr->dInfoPtr->x
			- rMargin;
		if (maxX < x) {
		    maxX = x;
		}
	    }
	}

	gotTab = false;
	maxBytes = segPtr->size - byteOffset;
	if (segPtr->typePtr == &tkTextCharType) {

	    /*
	     * See if there is a tab in the current chunk; if so, only layout
	     * characters up to (and including) the tab.
	     */

