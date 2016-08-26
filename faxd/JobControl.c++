/*	$Id: JobControl.c++ 1155 2013-04-26 22:39:33Z faxguy $ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
#include <ctype.h>

#include "Sys.h"

#include "JobControl.h"
#include "faxQueueApp.h"
#include "FaxTrace.h"
#include "FaxRequest.h"

#define	DCI_MAXCONCURRENTCALLS	0x0001
#define	DCI_TIMEOFDAY		0x0002
#define	DCI_MAXSENDPAGES	0x0004
#define	DCI_MAXDIALS		0x0008
#define	DCI_MAXTRIES		0x0010
#define	DCI_USEXVRES		0x0020
#define	DCI_VRES		0x0040
#define	DCI_PRIORITY		0x0080
#define	DCI_DESIREDDF		0x0100
#define	DCI_NOTIFY		0x0200
#define	DCI_USECOLOR		0x0400
#define	DCI_PROXYLOGMODE	0x0800
#define	DCI_PROXYTRIES		0x1000
#define	DCI_PROXYDIALS		0x2000
#define	DCI_PROXYRECONNECTS	0x4000

#define	isDefined(b)		(defined & b)
#define	setDefined(b)		(defined |= b)

JobControlInfo::JobControlInfo()		 	{ defined = 0; }
JobControlInfo::JobControlInfo(const JobControlInfo& other)
    : rejectNotice(other.rejectNotice)
    , modem(other.modem)
    , proxy(other.proxy)
    , proxyuser(other.proxyuser)
    , proxypass(other.proxypass)
    , tod(other.tod)
    , args(other.args)
{
    defined = other.defined;
    maxConcurrentCalls = other.maxConcurrentCalls;
    maxSendPages = other.maxSendPages;
    maxDials = other.maxDials;
    maxTries = other.maxTries;
    usexvres = other.usexvres;
    usecolor = other.usecolor;
    vres = other.vres;
    priority = other.priority;
    desireddf = other.desireddf;
    notify = other.notify;
    proxylogmode = other.proxylogmode;
    proxytries = other.proxytries;
    proxydials = other.proxydials;
    proxyreconnects = other.proxyreconnects;
    proxymailbox = other.proxymailbox;
    proxynotification = other.proxynotification;
    proxyjobtag = other.proxyjobtag;
}

JobControlInfo::JobControlInfo (const fxStr& buffer)
{
    defined = 0;
    u_int pos = 0;
    u_int last_pos = 0;
    int loop = 0;
    while ( (pos = buffer.next(last_pos, '\n')) < buffer.length() )
    {
    	// Quick safety-net
	if (loop++ > 100)
	    break;

    	fxStr l(buffer.extract(last_pos, pos - last_pos));
	last_pos = pos+1;

	readConfigItem(l);
    }
}

JobControlInfo::~JobControlInfo() {}

void
JobControlInfo::configError (const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fxStr::format("JobControl: %s", fmt) , ap);
    va_end(ap);
}

void
JobControlInfo::configTrace (const char*, ...)
{
   // We don't trace JobControl parsing...
}

bool
JobControlInfo::setConfigItem (const char* tag, const char* value)
{
    if (streq(tag, "rejectnotice")) {
	rejectNotice = value;
    } else if (streq(tag, "modem")) {
	modem = value;
    } else if (streq(tag, "proxy")) {
	proxy = value;
    } else if (streq(tag, "proxyuser")) {
	proxyuser = value;
    } else if (streq(tag, "proxypass")) {
	proxypass = value;
    } else if (streq(tag, "proxylogmode")) {
	setDefined(DCI_PROXYLOGMODE);
	proxylogmode = strtol(value, 0, 8);
    } else if (streq(tag, "proxytries")) {
	setDefined(DCI_PROXYTRIES);
	proxytries = getNumber(value);
    } else if (streq(tag, "proxydials")) {
	setDefined(DCI_PROXYDIALS);
	proxydials = getNumber(value);
    } else if (streq(tag, "proxyreconnects")) {
	setDefined(DCI_PROXYRECONNECTS);
	proxyreconnects = getNumber(value);
    } else if (streq(tag, "proxymailbox")) {
	proxymailbox = value;
    } else if (streq(tag, "proxynotification")) {
	proxynotification = value;
    } else if (streq(tag, "proxyjobtag")) {
	proxyjobtag = value;
    } else if (streq(tag, "maxconcurrentjobs")) {	// backwards compatibility
	maxConcurrentCalls = getNumber(value);
	setDefined(DCI_MAXCONCURRENTCALLS);
    } else if (streq(tag, "maxconcurrentcalls")) {
	maxConcurrentCalls = getNumber(value);
	setDefined(DCI_MAXCONCURRENTCALLS);
    } else if (streq(tag, "maxsendpages")) {
	maxSendPages = getNumber(value);
	setDefined(DCI_MAXSENDPAGES);
    } else if (streq(tag, "maxdials")) {
	maxDials = getNumber(value);
	setDefined(DCI_MAXDIALS);
    } else if (streq(tag, "maxtries")) {
	maxTries = getNumber(value);
	setDefined(DCI_MAXTRIES);
    } else if (streq(tag, "timeofday")) {
	tod.parse(value);
	setDefined(DCI_TIMEOFDAY);
    } else if (streq(tag, "usexvres")) {
	usexvres = getNumber(value);
	setDefined(DCI_USEXVRES);
    } else if (streq(tag, "usecolor")) {
	usecolor = getNumber(value);
	setDefined(DCI_USECOLOR);
    } else if (streq(tag, "vres")) {
	vres = getNumber(value);
	setDefined(DCI_VRES);
    } else if (streq(tag, "priority")) {
	priority = getNumber(value);
	setDefined(DCI_PRIORITY);
    } else if (streq(tag, "notify")) {
	notify = -1;
	if (strcmp("none", value) == 0) notify = FaxRequest::no_notice;
	if (strcmp("when done", value) == 0) notify = FaxRequest::when_done;
	if (strcmp("when requeued", value) == 0) notify = FaxRequest::when_requeued;
	if (strcmp("when done+requeued", value) == 0) notify = FaxRequest::notify_any;
	if (notify != -1) setDefined(DCI_NOTIFY);
    } else {
	if (streq(tag, "desireddf")) {		// need to pass desireddf to faxsend, also
	    desireddf = getNumber(value);
	    setDefined(DCI_DESIREDDF);
	}
	if( args != "" )
	    args.append('\0');
	args.append(fxStr::format("-c%c%s:\"%s\"",
	    '\0', tag, (const char*) value));
    }
    return true;
}

u_int
JobControlInfo::getMaxConcurrentCalls() const
{
    if (isDefined(DCI_MAXCONCURRENTCALLS))
	return maxConcurrentCalls;
    else
	return faxQueueApp::instance().getMaxConcurrentCalls();
}

u_int
JobControlInfo::getMaxSendPages() const
{
    if (isDefined(DCI_MAXSENDPAGES))
	return maxSendPages;
    else
	return faxQueueApp::instance().getMaxSendPages();
}

u_int
JobControlInfo::getMaxDials() const
{
    if (isDefined(DCI_MAXDIALS))
	return maxDials;
    else
	return faxQueueApp::instance().getMaxDials();
}

u_int
JobControlInfo::getMaxTries() const
{
    if (isDefined(DCI_MAXTRIES))
	return maxTries;
    else
	return faxQueueApp::instance().getMaxTries();
}

const fxStr&
JobControlInfo::getRejectNotice() const
{
    return rejectNotice;
}

const fxStr&
JobControlInfo::getModem() const
{
    return modem;
}

const fxStr&
JobControlInfo::getProxy() const
{
    return proxy;
}

const fxStr&
JobControlInfo::getProxyUser() const
{
    return proxyuser;
}

const fxStr&
JobControlInfo::getProxyPass() const
{
    return proxypass;
}

const fxStr&
JobControlInfo::getProxyMailbox() const
{
    return proxymailbox;
}

const fxStr&
JobControlInfo::getProxyNotification() const
{
    return proxynotification;
}

const fxStr&
JobControlInfo::getProxyJobTag() const
{
    return proxyjobtag;
}

const mode_t
JobControlInfo::getProxyLogMode() const
{
    if (isDefined(DCI_PROXYLOGMODE))
	return proxylogmode;
    else
	return(0600);
}

int
JobControlInfo::getProxyTries() const
{
    if (isDefined(DCI_PROXYTRIES))
	return proxytries;
    else
	return(-1);
}

int
JobControlInfo::getProxyDials() const
{
    if (isDefined(DCI_PROXYDIALS))
	return proxydials;
    else
	return(-1);
}

int
JobControlInfo::getProxyReconnects() const
{
    if (isDefined(DCI_PROXYRECONNECTS))
	return proxyreconnects;
    else
	return(5);
}

time_t
JobControlInfo::nextTimeToSend(time_t t) const
{
    if (isDefined(DCI_TIMEOFDAY))
	return tod.nextTimeOfDay(t);
    else
	return faxQueueApp::instance().nextTimeToSend(t);
}

int
JobControlInfo::getUseXVRes() const
{
    if (isDefined(DCI_USEXVRES))
	return usexvres;
    else
	return -1;
}

int
JobControlInfo::getUseColor() const
{
    if (isDefined(DCI_USECOLOR))
	return usecolor;
    else
	return -1;
}

u_int
JobControlInfo::getVRes() const
{
    if (isDefined(DCI_VRES))
	return vres;
    else
	return 0;
}

int
JobControlInfo::getPriority() const
{
    if (isDefined(DCI_PRIORITY))
	return priority;
    else
	return -1;
}

int
JobControlInfo::getDesiredDF() const
{
    if (isDefined(DCI_DESIREDDF))
	return desireddf;
    else
	return -1;
}

int
JobControlInfo::getNotify() const
{
    if (isDefined(DCI_NOTIFY))
	return notify;
    else
	return -1;
}

bool
JobControlInfo::isNotify(u_int what) const
{
    if (isDefined(DCI_NOTIFY) && (notify & (u_short) what) != 0)
	return true;
    else
	return false;
}
