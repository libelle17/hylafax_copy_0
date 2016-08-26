/*	$Id: MsgFmt.c++ 951 2009-10-28 23:41:42Z faxguy $ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */
#include "MsgFmt.h"
#include "StackBuffer.h"
#include "TextFormat.h"

#include <ctype.h>

MsgFmt::MsgFmt()
{
}

MsgFmt::MsgFmt(const MsgFmt& other)
    : boldFont(other.boldFont)
    , italicFont(other.italicFont)
    , headToKeep(other.headToKeep)
#ifdef notdef
    , headMap(other.headMap)
#endif
{
    headerStop = other.headerStop;
    verbose = other.verbose;
}

MsgFmt::~MsgFmt()
{
}

const fxStr*
MsgFmt::findHeader(const fxStr& name) const
{
    for (u_int i = 0, n = fields.length(); i < n; i++)
	if (strcasecmp(fields[i], name) == 0)
	    return (&headers[i]);
    return (NULL);
}

fxStr
MsgFmt::mapHeader(const fxStr& name)
{
    for (fxStrDictIter hi(headMap); hi.notDone(); hi++)
	if (strcasecmp(hi.key(), name) == 0)
	    return (hi.value());
    return (name);
}

bool
MsgFmt::getLine(FILE* fd, fxStackBuffer& buf)
{
    buf.reset();
    for (;;) {
	int c = getc(fd);
	if (c == EOF)
	    return (buf.getLength() > 0);
	c &= 0xff;
	if (c == '\r') {
	    c = getc(fd);
	    if (c == EOF)
		return (buf.getLength() > 0);
	    c &= 0xff;
	    if (c != '\n') {
		ungetc(c, fd);
		c = '\r';
	    }
	}
	if (c == '\n')
	    break;
	buf.put(c);
    }
    return (true);
}

/*
 * This function replaces comments with a single white space.
 * Unclosed comments are automatically closed at end of string.
 * Stray closing parentheses are left untouched, as are other invalid chars.
 * Headers which can contain quoted strings should not go through this
 * revision of this function as is doesn't honnor them and could end up doing
 * the wrong thing.
 */
fxStr
MsgFmt::stripComments(const fxStr& s)
{
    fxStr q;
    u_int depth = 0;
    bool wasSpace = true;
    for (u_int i = 0; i < s.length(); i++) {
        switch (s[i]) {
            case '(':
                depth++;
                break;
            case ')':
                if (depth > 0)
                    depth--;
                break;
            case '\\':
                if (depth == 0) {
                    q.append(s[i++]);     // Don't decode them at this time
                    q.append(s[i]);
                    wasSpace = false;
                } else
                  i++;
                break;
            default:
                if (depth == 0) {
                    if (!isspace(s[i]) || !wasSpace) {       // Trim consecutive spaces
                        q.append(s[i]);
                        wasSpace = isspace(s[i]);
                    }
                }
                break;
        }
    }
    while (q.length() > 0 && isspace(q[q.length()-1]))
      q.remove(q.length()-1, 1);      // Trim trailing white space
    return q;
}

void
MsgFmt::parseHeaders(FILE* fd, u_int& lineno)
{
    fxStackBuffer buf;
    fxStr field;				// current header field
    while (getLine(fd, buf)) {
	lineno++;
	if (buf.getLength() == 0)
	    break;
	/*
	 * Collect field name in a canonical format.
	 * If the line begins with whitespace, then
	 * it's the continuation of a previous header.
	 */ 
	fxStr line(&buf[0], buf.getLength());
	u_int len = line.length();
	while (len > 0 && isspace(line[line.length()-1])) {
	    line.remove(line.length()-1, 1);	// trim trailing whitespace
	    len--;
	}
	if (len > 0 && !isspace(line[0])) { 
	    u_int l = 0;
	    field = line.token(l, ':');
	    if (field != "" && l <= len) {	// record new header
		fields.append(field);
		// skip leading whitespace
		for (; l < len && isspace(line[l]); l++)
		    ;
		headers.append(line.tail(len-l));
		if (verbose)
		    fprintf(stderr, "HEADER %s: %s\n"
			, (const char*) fields[fields.length()-1]
			, (const char*) headers[headers.length()-1]
		    );
	    }
	} else if (field != "" && headers.length())  {		// append continuation
	    headers[headers.length()-1].append(line);
	    if (verbose)
		fprintf(stderr, "+HEADER %s: %s\n"
		    , (const char*) field
		    , (const char*) line
		);
	}
    
    }

    /*
     * Scan envelope for any meta-headers that
     * control how formatting is to be done.
     */
    for (u_int i = 0, n = fields.length();  i < n; i++) {
	const fxStr& field = fields[i];
	if (strncasecmp(field, "x-fax-", 6) == 0)
	    setConfigItem(&field[6], headers[i]);
    }
}

void
MsgFmt::setupConfig()
{
    verbose = false;
    boldFont = "Helvetica-Bold";
    italicFont = "Helvetica-Oblique";

    headToKeep.resize(0);
    headToKeep.append("To");
    headToKeep.append("From");
    headToKeep.append("Subject");
    headToKeep.append("Cc");
    headToKeep.append("Date");

    for (fxStrDictIter iter(headMap); iter.notDone(); iter++)
	headMap.remove(iter.key());
}

#undef streq
#define	streq(a,b)	(strcasecmp(a,b)==0)

bool
MsgFmt::setConfigItem(const char* tag, const char* value)
{
    if (streq(tag, "headers")) {
        char* cp = strcpy(new char[strlen(value) + 1], value);
        char* tp;
        do {
            tp = strchr(cp, ' ');
            if (tp) {
                *tp++ = '\0';
            }
            if (streq(cp, "clear")) {
                headToKeep.resize(0);
            } else {
                headToKeep.append(cp);
            }
        } while ((cp = tp));
        delete [] cp;
    } else if (streq(tag, "mapheader")) {
	char* tp = (char *) strchr(value, ' ');
	if (tp) {
	    for (*tp++ = '\0'; isspace(*tp); tp++)
		;
	    headMap[value] = tp;
	}
    } else if (streq(tag, "boldfont")) {
	boldFont = value;
    } else if (streq(tag, "italicfont")) {
	italicFont = value;
    } else if (streq(tag, "verbose")) {
	verbose = FaxConfig::getBoolean(tag);
    } else
	return (false);
    return (true);
}
#undef streq

u_int
MsgFmt::headerCount(void)
{
    u_int nl = 0;
    for (u_int i = 0, n = headToKeep.length(); i < n; i++)
	if (findHeader(headToKeep[i]))
	    nl++;				// XXX handle wrapped lines
    return (nl);
}

#ifdef roundup
#undef roundup
#endif
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

void
MsgFmt::formatHeaders(TextFormat& fmt)
{
    /*
     * Calculate tab stop for headers based on the
     * maximum width of the headers we are to keep.
     */
    const TextFont* bold = fmt.getFont("Bold");
    if (!bold)
	bold = fmt.addFont("Bold", boldFont);
    headerStop = 0;
    u_int i;
    u_int nHead = headToKeep.length();
    for (i = 0; i < nHead; i++) {
	TextCoord w = bold->strwidth(mapHeader(headToKeep[i]));
	if (w > headerStop)
	    headerStop = w;
    }
    headerStop += bold->charwidth(':');
    TextCoord boldTab = 8 * bold->charwidth(' ');
    headerStop = roundup(headerStop, boldTab);

    /*
     * Format headers we want; embolden field name
     * and italicize field value.  We wrap long
     * items to the field tab stop calculated above.
     */
    u_int nl = 0;
    for (i = 0; i < nHead; i++) {
	const fxStr* value = findHeader(headToKeep[i]);
	if (value) {
	    fxStr v(*value);
	    decodeRFC2047(v);
	    fmt.beginLine();
		TextCoord hm = bold->show(fmt.getOutputFile(),
		    mapHeader(headToKeep[i]) | ":");
		fmt.hrMove(headerStop - hm);
		showItalic(fmt, v);
	    fmt.endLine();
	    nl++;
	}
    }
    if (nl > 0) {
	/*
	 * Insert a blank line between the envelope and the
	 * body.  Note that we ``know too much here''--we
	 * know to insert whitespace below to insure valid
	 * PostScript is generated (sigh).
	 */
	fmt.beginLine();
	    fputc(' ', fmt.getOutputFile());	// XXX whitespace needed
	fmt.endLine();
    }
}

/*
 * Display the string in italic, wrapping to the
 * field header tab stop on any line overflows.
 */
void
MsgFmt::showItalic(TextFormat& fmt, const char* cp)
{
    const TextFont* italic = fmt.getFont("Italic");
    if (!italic)
	italic = fmt.addFont("Italic", italicFont);
    while (isspace(*cp))			// trim leading whitespace
	cp++;
    TextCoord x = fmt.getXOff();		// current x position on line
    FILE* tf = fmt.getOutputFile();		// output stream
    const char* tp = cp;
    for (; *tp != '\0'; tp++) {
	if (*tp == '\r' && *(tp+1) == '\n') tp++;
	TextCoord hm = italic->charwidth(*tp);
	if (*tp == '\n' || x+hm > fmt.getRHS()) {// text would overflow line
	    italic->show(tf, cp, tp-cp), cp = tp;// flush pending text
	    if (!fmt.getLineWrapping())		// truncate line, don't wrap
		return;
	    fmt.endLine();			// terminate line
	    fmt.beginLine();			// begin another line
	    fmt.hrMove(headerStop);		// reposition to header stop
	    x = fmt.getXOff();
	    if (*tp == '\n') {			// remove leading white space
		for (tp++; isspace(*tp); tp++)
		    ;
		cp = --tp;
	    }
	}
	x += hm;
    }
    if (tp > cp)
	italic->show(tf, cp, tp-cp);		// flush remainder
}

static int
hex(char c)
{
    // NB: don't check if c is in set [0-9a-fA-F]
    return (isdigit(c) ? c-'0' : isupper(c) ? 10+c-'A' : 10+c-'a');
}

#define ishex(c) ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))

/*
 * Decode a string that is RFC2047-encoded
 */
void
MsgFmt::decodeRFC2047(fxStr& s)
{
    fxStackBuffer buf;
    u_int bm = s.find(0, "=?");			// beginning marker
    if (bm < s.length()) {
	u_int mm = s.find(bm+2, "?Q?");		// quoted printable
	if (mm == s.length()) s.find(bm, "?q?");
	if (mm < s.length()) {
	    u_int em = s.find(mm+3, "?=");	// end marker
	    if (em < s.length()) {
		s.remove(em, 2);
		s.remove(bm, mm-bm+3);
		u_int i = 0;
		while (i < s.length()) {
		    if (s[i] == '_') s[i] = ' ';
		    i++;
		}
		copyQP(buf, s, s.length());
		buf.put('\0');
		s = buf;
	    }
	} else {
	    s.find(bm, "?B?");			// base64
	    if (mm == s.length()) s.find(bm, "?b?");
	    if (mm < s.length()) {
		u_int em = s.find(mm+3, "?=");	// end marker
		if (em < s.length()) {
		    s.remove(em, 2);
		    s.remove(bm, mm-bm+3);
		    b64State state;
		    state.c1 = -1;
		    state.c2 = -1;
		    state.c3 = -1;
		    state.c4 = -1;
		    copyBase64(buf, s, s.length(), state);
		    buf.put('\0');
		    s = buf;
		}
	    }
	}
    }
}

void
MsgFmt::copyQP(fxStackBuffer& buf, const char line[], u_int cc)
{
    // copy to buf & convert =XX escapes
    for (u_int i = 0; i < cc; i++) {
	if (line[i] == '=' && cc-i >= 2) {
	    int v1 = hex(line[++i]);
	    int v2 = hex(line[++i]);
	    buf.put((v1<<4) + v2);
	} else
	    buf.put(line[i]);
    }
}

void
MsgFmt::copyBase64(fxStackBuffer& buf, const char line[], u_int cc, b64State& state)
{
    static const int base64[128] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
	15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
	41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };

    int putted = 0;
    if (state.c2 != -1) putted = 1;
    if (state.c3 != -1) putted = 2;
    if (state.c4 != -1) putted = 3;
    state.l = 0;

    u_int i = 0;
    while (i < cc) {
	if (state.c1 == -1) {
	    do {
		if (i < cc) state.c1 = base64[(u_int)line[i++]];
	    } while (state.c1 < 0 && i < cc);
	}
	if (state.c1 >= 0) {
	    state.l = i;
	    if (state.c2 == -1) {
		do {
		    if (i < cc) state.c2 = base64[(u_int)line[i++]];
		} while (state.c2 < 0 && i < cc);
	    }
	    if (state.c2 >= 0) {
		state.l = i;
		if (putted <  1) buf.put((state.c1<<2) | ((state.c2&0x30)>>4));
		if (state.c3 == -1) {
		    do {
			if (i < cc) state.c3 = base64[(u_int)line[i++]];
		    } while (state.c3 < 0 && i < cc);
		}
		if (state.c3 >= 0) {
		    state.l = i;
		    if (putted < 2) buf.put(((state.c2&0x0f)<<4) | ((state.c3&0x3c)>>2));
		    if (state.c4 == -1) {
			do {
			    if (i < cc) state.c4 = base64[(u_int)line[i++]];
			} while (state.c4 < 0 && i < cc);
		    }
		    if (state.c4 >= 0) {
			state.l = i;
			if (putted < 3) buf.put((state.c3&0x3)<<6 | state.c4);
			state.c1 = -1;
			state.c2 = -1;
			state.c3 = -1;
			state.c4 = -1;
			putted = 0;
		    } else if (state.c4 < 0) state.l = i;
		} else if (state.c3 < 0) state.l = i;
	    } else if (state.c2 < 0) state.l = i;
	} else if (state.c1 < 0) state.l = i;
    }
}

inline int DEC(char c) { return ((c - ' ') & 077); }

void
MsgFmt::copyUUDecode(fxStackBuffer& buf, const char line[], u_int)
{
    const char* cp = line;
    int n = DEC(*cp);
    if (n > 0) {
	// XXX check n against passed in byte count for line
	int c;
	for (cp++; n >= 3; cp += 4, n -= 3) {
	    c = (DEC(cp[0])<<2) | (DEC(cp[1])>>4); buf.put(c);
	    c = (DEC(cp[1])<<4) | (DEC(cp[2])>>2); buf.put(c);
	    c = (DEC(cp[2])<<6) |  DEC(cp[3]);     buf.put(c);
	}
	if (n >= 1)
	    c = (DEC(cp[0])<<2) | (DEC(cp[1])>>4), buf.put(c);
	if (n >= 2)
	    c = (DEC(cp[1])<<4) | (DEC(cp[2])>>2), buf.put(c);
	if (n >= 3)
	    c = (DEC(cp[2])<<6) |  DEC(cp[3]),     buf.put(c);
    }
}
