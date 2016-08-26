/*	$Id: MIMEState.c++ 1032 2010-11-23 03:04:23Z faxguy $ */
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
/*
 * MIME Decoder Support.
 */
#include "MIMEState.h"
#include "MsgFmt.h"
#include "StackBuffer.h"

#include <string.h>
#include <ctype.h>

MIMEState::MIMEState(const char* t, const char* st) : type(t), subtype(st)
{
    parent = NULL;
    lastPart = false;
    encode = ENC_7BIT;
    charset = CS_USASCII;
    blen = (u_int) -1;			// NB: should insure no matches
    lineno = 1;
    external = false;
}
MIMEState::MIMEState(MIMEState& other)
    : parent(&other)
    , boundary(other.boundary)
{
    if (other.type == "multipart" && other.subtype == "digest") 
	type = "message", subtype = "rfc822";
    else
	type = "text", subtype = "plain";
    lastPart = false;
    encode = other.encode;
    charset = other.charset;
    blen = other.blen;
    lineno = other.lineno;
    external = other.external;
}
MIMEState::MIMEState(MIMEState& other, const char* t, const char* st)
    : parent(&other)
    , type(t)
    , subtype(st)
    , boundary(other.boundary)
{
    lastPart = false;
    encode = other.encode;
    charset = other.charset;
    blen = other.blen;
    lineno = other.lineno;
    external = other.external;
}
MIMEState::~MIMEState()
{
    if (parent) {
	parent->lineno = lineno;
    }
}

void
MIMEState::trace(FILE* fd)
{
    static const char* enames[] = {
	"7bit",
	"quoted-printable",
	"base64",
	"8bit",
	"binary",
	"x-uuencode"
    };
    static const char* cnames[] = {
	"us-ascii",
	"utf-8",
	"iso-8859-1",
	"iso-8859-2",
	"iso-8859-3",
	"iso-8859-4",
	"iso-8859-5",
	"iso-8859-6",
	"iso-8859-7",
	"iso-8859-8",
	"iso-8859-9",
    };
    fprintf(fd, "MIME part (line %u): %s/%s charset=%s encoding=%s\n"
	, lineno
	, (const char*) type
	, (const char*) subtype
	, cnames[charset]
	, enames[encode]
    );
}

bool
MIMEState::parseToken(const char*& cp, const char delimeter, fxStr& result)
{
    while (*cp && isspace(*cp))
	cp++;
    const char* bp = cp;
    while (*cp && !isspace(*cp) && *cp != delimeter)
	cp++;
    if (cp - bp > 0) {
	result = fxStr(bp, cp-bp);
	while (isspace(*cp))				// remove trailing ws
	    cp++;
	return (true);
    } else
	return (false);
}

/*
 * Extract MIME-related information from a set of
 * message headers and fillin the MIME state block.
 */
bool
MIMEState::parse(const MsgFmt& msg, fxStr& emsg)
{
    const fxStr* s = msg.findHeader("Content-Type");
    if (s) {				// type/subtype [; parameter]*
	const char* cp = &(*s)[0];
	if (parseToken(cp, '/', type)
	  && *cp++ == '/'
	  && parseToken(cp, ';', subtype)) {
	    if (*cp == ';')		// parse optional parameters
		parseParameters(cp+1);
	    type.lowercase();
	    subtype.lowercase();
	} else {
	    emsg = "Syntax error parsing MIME Content-Type: " | *s;
	    type = "text";		// reset on parsing error
	    return (false);
	}
    }
    s = msg.findHeader("Content-Transfer-Encoding");
    if (s)
	setEncoding(&(*s)[0]);
    s = msg.findHeader("Content-Description");
    if (s)
	desc = *s;
    s = msg.findHeader("Content-ID");
    if (s)
	cid = *s;
    s = msg.findHeader("Content-Disposition");
    if (s)
	disp = *s;
    return (true);
}

/*
 * Parse optional MIME parameters used by the decoder.
 */
void
MIMEState::parseParameters(const char* cp)
{
    for (;;) {
	while (*cp && isspace(*cp))
	    cp++;
	const char* bp = cp;
	while (*cp && !isspace(*cp) && *cp != '=')
	    cp++;
	if (*cp == '\0')			// end of string
	    break;
	fxStr param(bp, cp-bp);
	if (*cp != '=') {			// param<ws>=
	    while (isspace(*cp++))
		;
	    if (*cp != '=')			// syntax error
		break;
	}
	for (cp++; isspace(*cp); cp++)		// skip ws after =
	    ;
	const char* ev;
	if (*cp == '"') {			// quoted value
	    for (ev = ++cp; *ev && *ev != '"'; ev++)
		;
	    if (*ev != '"')			// missing close quote
		break;
	} else {
	    for (ev = cp; *ev && !isspace(*ev) && *ev != ';'; ev++)
		;
	}

	(void) setParameter(param, fxStr(cp, ev-cp));

	if (*ev != '\0' && *ev != ';') {	// scan to trailing ;
	    do {
		ev++;
	    } while (*ev && *ev != ';');
	}
	if (*ev != ';')
	    break;
	cp = ++ev;
    }
}

/*
 * Set a MIME parameter used by the decoder.
 */
bool
MIMEState::setParameter(const fxStr& p, const fxStr& value)
{
    fxStr param(p);
    param.lowercase();
    if (param.length() == 7 && param == "charset") {
	setCharset(value);			// character set
    } else if (param.length() == 8 && param == "boundary") {
	setBoundary(value);			// part boundary marker
    } else
	return (false);
    return (true);
}

/*
 * Set the content-transfer-encoding.
 */
void
MIMEState::setEncoding(const char* cp)
{
    if (strcasecmp(cp, "7bit") == 0)
	encode = ENC_7BIT;
    else if (strcasecmp(cp, "quoted-printable") == 0)
	encode = ENC_QPRINT;
    else if (strcasecmp(cp, "base64") == 0)
	encode = ENC_BASE64;
    else if (strcasecmp(cp, "8bit") == 0)
	encode = ENC_8BIT;
    else if (strcasecmp(cp, "binary") == 0)
	encode = ENC_BINARY;
    else if (strcasecmp(cp, "x-uuencode") == 0)
	encode = ENC_UUENCODE;
}

/*
 * Set the body part boundary.
 */
void
MIMEState::setBoundary(const char* s)
{
    boundary = s;
    boundary.insert("--");		// NB: insert "--" to simplify parsing
    boundary.append("--");		// NB: append "--" to simplify parsing
    blen = boundary.length()-2;		// optimized for checking
}

/*
 * Set the text character set.
 */
void
MIMEState::setCharset(const char* cp)
{
    if (strcasecmp(cp, "us-ascii") == 0)
	charset = CS_USASCII;
    else if (strcasecmp(cp, "utf-8") == 0)
	charset = CS_UTF8;
    else if (strcasecmp(cp, "iso-8859-1") == 0)
	charset = CS_ISO8859_1;
    else if (strcasecmp(cp, "iso-8859-2") == 0)
	charset = CS_ISO8859_2;
    else if (strcasecmp(cp, "iso-8859-3") == 0)
	charset = CS_ISO8859_3;
    else if (strcasecmp(cp, "iso-8859-4") == 0)
	charset = CS_ISO8859_4;
    else if (strcasecmp(cp, "iso-8859-5") == 0)
	charset = CS_ISO8859_5;
    else if (strcasecmp(cp, "iso-8859-6") == 0)
	charset = CS_ISO8859_6;
    else if (strcasecmp(cp, "iso-8859-7") == 0)
	charset = CS_ISO8859_7;
    else if (strcasecmp(cp, "iso-8859-8") == 0)
	charset = CS_ISO8859_8;
    else if (strcasecmp(cp, "iso-8859-9") == 0)
	charset = CS_ISO8859_9;
}

extern void fxFatal(const char* fmt ...);

/*
 * Return a line of data according to the current
 * setting of the content-transfer-encoding and
 * the body part boundary.  If the boundary marker
 * or EOF is reached false is returned, otherwise
 * this method returns true and the decoded line.
 * If the data is encoded with a text-oriented
 * scheme (7bit, 8bit, binary, or quoted-printable)
 * then the trailing newline character is returned
 * in the buffer.  Otherwise any trailing newline
 * is discarded and only the decoded data is returned.
 */
bool
MIMEState::getLine(FILE* fd, fxStackBuffer& buf)
{
    buf.reset();
    switch (encode) {
    case ENC_7BIT:
        for (;;) {
            int c = getc(fd);
            if (c == EOF) {
                return (buf.getLength() > 0);
            }
            c &= 0xff;
	    if (c == '\r') {
		c = getc(fd);
		if (c == EOF) {
		    return (buf.getLength() > 0);
		}
		c &= 0xff;
		if (c != '\n') {
		    ungetc(c, fd);
		    c = '\r';
		}
	    }
            if (c == '\n') {			// check for boundary marker
		lineno++;
	        u_int cc = buf.getLength();
	        if (cc >= blen && buf[0] == '-') {
	            if (cc == blen && strneq(buf, boundary, blen)) {
			buf.put('\n');
			return (false);
		    }
		    if (cc == blen+2 && strneq(buf, boundary, blen+2)) {
			lastPart = true;
			buf.put('\n');
			return (false);
		    }
		}
                buf.put('\n');
                return (true);
            }
            buf.put(c);
        }
	    /*NOTREACHED*/
    case ENC_8BIT:
    case ENC_BINARY:
        for (;;) {
            int c = getc(fd);
            if (c == EOF) {
	            return (buf.getLength() > 0);
            }
            c &= 0xff;
	    if (c == '\r') {
		c = getc(fd);
		if (c == EOF) {
		    return (buf.getLength() > 0);
		}
		c &= 0xff;
		if (c != '\n') {
		    ungetc(c, fd);
		    c = '\r';
		}
	    }
            if (c == '\n') {			// check for boundary marker
		lineno++;
		u_int cc = buf.getLength();
		if (cc >= blen && buf[0] == '-') {
		    if (cc == blen && strneq(buf, boundary, blen)) {
 			buf.put('\n');
			return (false);
		    }
		    if (cc == blen+2 && strneq(buf, boundary, blen+2)) {
			lastPart = true;
 			buf.put('\n');
			return (false);
		    }
	        }
		buf.put('\n');
		return (true);
            }
            buf.put(c);
        }
	    /*NOTREACHED*/
    case ENC_QPRINT:	return getQuotedPrintableLine(fd, buf);
    case ENC_BASE64:	return getBase64Line(fd, buf);
    case ENC_UUENCODE:	return getUUDecodeLine(fd, buf);
    }
    fxFatal("Internal error, unsupported Content-Transfer-Encoding %u", encode);
    /*NOTREACHED*/
    return (false);
}

/*
 * Return a decoded line of quoted-printable text.
 */
bool
MIMEState::getQuotedPrintableLine(FILE* fd, fxStackBuffer& buf)
{
    MsgFmt msg;
    char line[80];				// spec says never more than 76
    u_int cc = 0;				// chars in current line

    /*
     * Some encoders, in-particular Microsoft Outlook, can make a poor decision to encode some
     * binary file types (such as PDF) with quoted-printable.  In such cases we have to decide
     * whether a line-break ('\n') should be decoded as LF ('\n') or CR+LF ('\r\n').  To be 
     * thorough we could pattern-match the X-Mailer with known perpetrators, but for now we
     * simply check to see if the Content-Type is one that really should not be encoded with
     * quoted-printable.  If it is, then we assume that it was one where line-breaks should
     * be decoded as CR+LF (because these are the only known perpetrators at this time). If
     * this assumption later proves to be false, then we'll probably need to also use X-Mailer
     * as a deciding factor.
     */
    bool breakhascr = false;
    if (strneq(type, "application", 11) && strneq(subtype, "pdf", 3)) breakhascr = true;

    for (;;) {
	int c = getc(fd);
	if (c == EOF) {
	    msg.copyQP(buf, line, cc);
	    return (buf.getLength() > 0);
	}
	c &= 0xff;
	if (c == '\r') {
	    c = getc(fd);
	    if (c == EOF) {
		msg.copyQP(buf, line, cc);
		return (buf.getLength() > 0);
	    }
	    c &= 0xff;
	    if (c != '\n') {
		ungetc(c, fd);
		c = '\r';
	    }
	}
	if (c == '\n') {			// check for boundary marker first
	    lineno++;
	    if (cc >= blen && line[0] == '-') {
		if (cc == blen && strneq(line, boundary, blen)) {
		    if (breakhascr) buf.put('\r');
		    buf.put('\n');
		    return (false);
		}
		if (cc == blen+2 && strneq(line, boundary, blen+2)) {
		    lastPart = true;
		    if (breakhascr) buf.put('\r');
		    buf.put('\n');
		    return (false);
		}
	    }
	    if (cc > 0 && line[cc-1] == '=') {	// soft line break
		msg.copyQP (buf, line, cc-1);       // everything up to ``="''
		cc = 0;
		continue;
	    }
	    msg.copyQP(buf, line, cc);
	    if (breakhascr) buf.put('\r');
	    buf.put('\n');
	    return (true);
	}
	if (cc < sizeof (line)-1)
	    line[cc++] = c;
    }
    /*NOTREACHED*/
}

/*
 * Return a decoded line of base64 data.
 */
bool
MIMEState::getBase64Line(FILE* fd, fxStackBuffer& buf)
{
    MsgFmt msg;
    char line[80];				// spec says never more than 76 chars in
    memset(line, 0, sizeof(line));		// current line, but some don't comply
    u_int cc = 0;
    b64State state;
    state.c1 = -1;
    state.c2 = -1;
    state.c3 = -1;
    state.c4 = -1;
    for (;;) {
	int c = getc(fd);
	if (c == EOF) {
	    msg.copyBase64(buf, line, cc, state);
	    if (state.l != cc)
		fprintf(stderr, "truncated base64 data detected\n");
	    return (buf.getLength() > 0);
	}
	c &= 0x7f;
	if (c == '\r') {
	    c = getc(fd);
	    if (c == EOF) {
		msg.copyBase64(buf, line, cc, state);
		if (state.l != cc)
		    fprintf(stderr, "truncated base64 data detected\n");
		return (buf.getLength() > 0);
	    }
	    c &= 0x7f;
	    if (c != '\n') {
		ungetc(c, fd);
		c = '\r';
	    }
	}
	if (c == '\n') {			// check for boundary marker
	    lineno++;
	    if (cc >= blen && line[0] == '-') {
		if (cc == blen && strneq(line, boundary, blen)) {
		    buf.put('\n');
		    return (false);
		}
		if (cc == blen+2 && strneq(line, boundary, blen+2)) {
		    lastPart = true;
		    buf.put('\n');
		    return (false);
		}
	    }
	    msg.copyBase64(buf, line, cc, state);
	    if (state.l != cc) {
		/*
		 * copyBase64 did not fully process the line and (cc-l) characters
		 * were left unhandled.  This probably means that the base64 encoder
		 * is not following spec and "wrapped" the base64 data across
		 * multiple lines without consideration to the encoding.  So, we'll
		 * need to preserve the last (cc-l) characters and proceed with
		 * the next line without returning.
		 */
		cc -= state.l;
		memmove(line, line+state.l, cc);
		memset(line+cc, 0, sizeof(line)-cc);
	    } else {
		return (true);
	    }
	}
	line[cc++] = c;
	if (cc >= sizeof(line)) {
	    /*
	     * The line is longer than the spec requires.  This probably means that
	     * base64 encoder is just running very long out-of-spec lines.  So, we
	     * need to process what we have got and come back for more.  This is
	     * similar to (and likely coupled with) the "wrapped" condition above.
	     */
	    msg.copyBase64(buf, line, cc, state);
	    cc -= state.l;
	    memmove(line, line+state.l, cc);
	    memset(line+cc, 0, sizeof(line)-cc);
	}
    }
    /*NOTREACHED*/
}

/*
 * Return a decoded line of uuencode'd data.
 */
bool
MIMEState::getUUDecodeLine(FILE* fd, fxStackBuffer& buf)
{
    MsgFmt msg;
    char line[80];				// spec says never more than 62
    u_int cc = 0;				// chars in current line
    for (;;) {
	int c = getc(fd);
	if (c == EOF) {
	    msg.copyUUDecode(buf, line, cc);
	    return (buf.getLength() > 0);
	}
	c &= 0x7f;
	if (c == '\r') {
	    c = getc(fd);
	    if (c == EOF) {
		msg.copyUUDecode(buf, line, cc);
		return (buf.getLength() > 0);
	    }
	    c &= 0x7f;
	    if (c != '\n') {
		ungetc(c, fd);
		c = '\r';
	    }
	}
	if (c == '\n') {
	    lineno++;
	    if (cc >= blen && line[0] == '-') {	// check for boundary marker
		if (cc == blen && strneq(line, boundary, blen)) {
		    buf.put('\n');
		    return (false);
		}
		if (cc == blen+2 && strneq(line, boundary, blen+2)) {
		    lastPart = true;
		    buf.put('\n');
		    return (false);
		}
	    } else if (cc >= 6 && strneq(line, "begin ", 6)) {
		return (getUUDecodeLine(fd, buf));
	    } else if (cc == 3 && streq(line, "end")) {
		// consume to boundary marker
		while (getUUDecodeLine(fd, buf))
		    ;
		return (false);
	    }
	    msg.copyUUDecode(buf, line, cc);
	    return (true);
	}
	if (cc < sizeof (line)-1)
	    line[cc++] = c;
    }
    /*NOTREACHED*/
}
