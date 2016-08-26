/*	$Id: Jobs.c++ 1120 2012-09-24 17:19:11Z faxguy $ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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

#include "port.h"
#include "config.h"
#include "Sys.h"
#include "HylaFAXServer.h"
#include "Dispatcher.h"
#include "Timeout.h"
#include "class2.h"

#include <sys/file.h>
#include <ctype.h>

/*
 * Job state query and control commands.
 */
Job::Job(const fxStr& qf, int f) : FaxRequest(qf,f)
{
    lastmod = 0;
    queued = false;
}
Job::~Job() {}

/*
 * We need to override the default logic because faxq
 * assumes it's working directory is at the top of the
 * spooling area and we may be elsewhere.  We can also
 * simplify things because we know we're chroot'd to
 * the top of the spooling area--thus we don't need to
 * check for ``..'' and ``/'' being used to reference
 * files outside the spooling area.
 */
bool
Job::checkDocument(const char* pathname)
{
    struct stat sb;
    if (FileCache::lookup(fxStr::format("/%s", pathname), sb))
	return (true);
    int fd = Sys::open(pathname, 0);
    if (fd == -1) {
	logError("Can not access document file \"%s\": %s",
	    pathname, strerror(errno));
	return (false);
    }
    Sys::close(fd);
    logError("Undetermined error with document file \"%s\"", pathname);
    return (false);
}

fxIMPLEMENT_StrKeyPtrValueDictionary(JobDict, Job*)

/*
 * Job state parameter access controls.
 *
 * There are three levels of access control applied to job state
 * information: administrator, owner, and other.
 * Access is controlled on a read+write basis.  Write accesses
 * can be further restricted to constrain written values to
 * be within a range; this is used to allow users to change
 * parameters within a restricted range (e.g. to up the maximum
 * number of tries but still constrain it to a sane value).
 */
#define	A_RUSR	0400	// read permission: owner
#define	A_WUSR	0200	// abitrary write permission: owner
#define	A_MUSR	0100	// restricted write permission: owner
#define	A_RADM	0040	// read permission: administrator
#define	A_WADM	0020	// abitrary write permission: administrator
#define	A_MADM	0010	// restricted write permission: administrator
#define	A_ROTH	0004	// read permission: other
#define	A_WOTH	0002	// abitrary write permission: other
#define	A_MOTH	0001	// restricted write permission: other

#define	A_READ	 004
#define	A_WRITE	 002
#define	A_MODIFY 001

#define	N(a)		(sizeof (a) / sizeof (a[0]))

static const struct {
    Token	t;
    u_int	protect;		// read+write protection
} params[] = {
    { T_BEGBR,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_BEGST,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_CHOPTHRESH,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_CLIENT,		A_RUSR|A_RADM|A_WADM|A_ROTH },
    { T_COMMID,		A_RUSR|A_RADM|A_ROTH },
    { T_COMMENTS,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TIMEOFDAY,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TIMEZONE,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_COVER,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_DATAFORMAT,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_DIALSTRING,	A_RUSR|A_WUSR|A_RADM|A_WADM },
    { T_DOCUMENT,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_DONEOP,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_EXTERNAL,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_FAXNUMBER,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_FAXNAME,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_FROM_COMPANY,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_FROM_LOCATION,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_FROM_USER,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_FROM_VOICE,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_GROUPID,	A_RUSR|A_RADM|A_ROTH },
    { T_IGNOREMODEMBUSY,A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_JOBID,		A_RUSR|A_RADM|A_ROTH },
    { T_JOBINFO,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_JOBTYPE,	A_RUSR|A_RADM|A_ROTH },
    { T_LASTTIME,	A_RUSR|A_MUSR|A_RADM|A_WADM|A_ROTH },
    { T_MAXDIALS,	A_RUSR|A_MUSR|A_RADM|A_WADM|A_ROTH },
    { T_MAXTRIES,	A_RUSR|A_MUSR|A_RADM|A_WADM|A_ROTH },
    { T_MINBR,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_MODEM,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_OWNER,		A_RUSR|A_RADM|A_WADM|A_ROTH },
    { T_NDIALS,		A_RUSR|A_RADM|A_ROTH },
    { T_NOTIFY,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_NOTIFYADDR,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_NPAGES,		A_RUSR|A_RADM|A_ROTH },
    { T_NTRIES,		A_RUSR|A_RADM|A_ROTH },
    { T_PAGECHOP,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_PAGELENGTH,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_PAGEWIDTH,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_PASSWD,		A_RUSR|A_WUSR|A_RADM|A_WADM },
    { T_POLL,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_REGARDING,	A_RUSR|A_MUSR|A_RADM|A_WADM|A_ROTH },
    { T_RETRYTIME,	A_RUSR|A_MUSR|A_RADM|A_WADM|A_ROTH },
    { T_SCHEDPRI,	A_RUSR|A_MUSR|A_RADM|A_WADM|A_ROTH },
    { T_SENDTIME,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_SKIPPAGES,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_SKIPPEDPAGES,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_NOCOUNTCOVER,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_PROBEONLY,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_CONNTIME,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_DURATION,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_STATE,		A_RUSR|A_RADM|A_ROTH },
    { T_STATUS,		A_RUSR|A_RADM|A_WADM|A_ROTH },
    { T_ERRORCODE,	A_RUSR|A_RADM|A_WADM|A_ROTH },
    { T_SERVERDOCOVER,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_SUBADDR,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TAGLINE,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TOTDIALS,	A_RUSR|A_RADM|A_ROTH },
    { T_TOTPAGES,	A_RUSR|A_RADM|A_ROTH },
    { T_TOTTRIES,	A_RUSR|A_RADM|A_ROTH },
    { T_TO_COMPANY,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TO_LOCATION,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TO_USER,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TO_VOICE,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_TSI,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_USE_CONTCOVER,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_USE_ECM,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_ECMTYPE,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_USE_TAGLINE,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_USE_XVRES,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_USE_COLOR,	A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_USRKEY,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_VRES,		A_RUSR|A_WUSR|A_RADM|A_WADM|A_ROTH },
    { T_NIL,		0 },
};

/*
 * Check client permission to do the specified operation
 * on the specified job state parameter.  Operation can
 * be a combination of A_READ+A_WRITE+A_MODIFY.  Note that
 * ownership is based on login account and not fax uid;
 * this may need to be rethought.
 */
bool
HylaFAXServer::checkAccess(const Job& job, Token t, u_int op)
{
    u_int m = 0;
    if (t == T_JOB) {
    	m = jobProtection;
    } else {
	u_int n = N(params)-1;
	u_int i = 0;
	while (i < n && params[i].t != t)
	    i++;
	m = params[i].protect;
    }
    if (m&op)					// other/public access
	return (true);
    if (IS(PRIVILEGED) && ((m>>3)&op))		// administrative access
	return (true);
    if (job.owner == the_user && ((m>>6)&op))	// owner access
	return (true);
    return (false);
}

static struct {
    Token	t;
    fxStr	Job::* p;
} strvals[] = {
    { T_JOBTYPE,	&Job::jobtype },
    { T_EXTERNAL,	&Job::external },
    { T_DIALSTRING,	&Job::number },
    { T_NOTIFYADDR,	&Job::mailaddr },
    { T_USRKEY,		&Job::jobtag },
    { T_MODEM,		&Job::modem },
    { T_TO_USER,	&Job::receiver },
    { T_TO_COMPANY,	&Job::company },
    { T_TO_LOCATION,	&Job::location },
    { T_TO_VOICE,	&Job::voice },
    { T_FROM_USER,	&Job::sender },
    { T_FROM_COMPANY,	&Job::fromcompany },
    { T_FROM_LOCATION,	&Job::fromlocation },
    { T_FROM_VOICE,	&Job::fromvoice },
    { T_PASSWD,		&Job::passwd },
    { T_CLIENT,		&Job::client },
    { T_FAXNUMBER,	&Job::faxnumber },
    { T_FAXNAME,	&Job::faxname },
    { T_TSI,		&Job::tsi },
    { T_TAGLINE,	&Job::tagline },
    { T_SUBADDR,	&Job::subaddr },
    { T_GROUPID,	&Job::groupid },
    { T_JOBID,		&Job::jobid },
    { T_JOBINFO,	&Job::jobtag },
    { T_OWNER,		&Job::owner },
    { T_STATUS,		&Job::notice },
    { T_ERRORCODE,	&Job::errorcode },
    { T_DONEOP,		&Job::doneop },
    { T_COMMID,		&Job::commid },
    { T_REGARDING,	&Job::regarding },
    { T_COMMENTS,	&Job::comments },
    { T_TIMEOFDAY,	&Job::timeofday },
    { T_TIMEZONE,	&Job::timezone },
};
static struct {
    Token	t;
    u_short	Job::* p;
} shortvals[] = {
    { T_TOTPAGES,	&Job::totpages },
    { T_NPAGES,		&Job::npages },
    { T_SKIPPAGES,	&Job::skippages },
    { T_NOCOUNTCOVER,	&Job::nocountcover },
    { T_PROBEONLY,	&Job::probeonly },
    { T_CONNTIME,	&Job::conntime },
    { T_DURATION,	&Job::duration },
    { T_NTRIES,		&Job::ntries },
    { T_NDIALS,		&Job::ndials },
    { T_TOTDIALS,	&Job::totdials },
    { T_MAXDIALS,	&Job::maxdials },
    { T_TOTTRIES,	&Job::tottries },
    { T_MAXTRIES,	&Job::maxtries },
    { T_PAGEWIDTH,	&Job::pagewidth },
    { T_PAGELENGTH,	&Job::pagelength },
    { T_VRES,		&Job::resolution },
    { T_SCHEDPRI,	&Job::usrpri },
    { T_MINBR,		&Job::minbr },
    { T_BEGBR,		&Job::desiredbr },
    { T_BEGST,		&Job::desiredst },
};
static struct {
    Token	t;
    int		Job::* p;
} intvals[] = {
    { T_SKIPPEDPAGES,	&Job::skippedpages },
};
static const char* notifyVals[4] = {
    "NONE",		// no_notice
    "DONE",		// when_done
    "REQUEUE",		// when_requeued
    "DONE+REQUEUE"	// when_done|when_requeued
};
static const char* chopVals[4] = {
    "DEFAULT",		// chop_default
    "NONE",		// chop_none
    "ALL",		// chop_all
    "LAST"		// chop_last
};
static const char* dataVals[] = {
    "G31D",		// Group 3, 1-D
    "G32D",		// Group 3, 2-D
    "G32DUNC",		// Group 3, 2-D (w/ uncompressed)
    "G4",		// Group 4
    "JBIG",		// JBIG
    "JPEG-GREY",	// JPEG greyscale
    "JPEG-COLOR"	// JPEG color
};
static const char* ecmVals[] = {
    "NONE",		// 0 = no ECM
    "64BIT",		// 64-bit T.30-A ECM
    "256BIT",		// 256-bit T.30-A ECM
    "HALFDUPLEX",	// Half Duplex T.30-C ECM
    "FULLDUPLEX"	// Full Duplex T.30-C ECM
};
static const char* stateVals[] = {
    "UNDEFINED",	// undefined state (should never be used)
    "SUSPENDED",	// not being scheduled
    "PENDING",		// waiting for time to send
    "SLEEPING",		// waiting for scheduled timeout
    "BLOCKED",		// blocked by concurrent activity
    "READY",		// ready to be go, waiting for resources
    "ACTIVE",		// actively being processed
    "DONE",		// processing completed with success
    "FAILED",		// processing completed with failure
};
static const char* docTypeNames[] = {
    "FAX",		// send_fax
    "TIFF",		// send_tiff
    "TIFF",		// send_tiff_saved
    "PDF",		// send_pdf
    "PDF",		// send_pdf_saved
    "PS",		// send_postscript
    "PS",		// send_postscript_saved
    "PCL",		// send_pcl
    "PCL",		// send_pcl_saved
    "DATA",		// send_data
    "DATA",		// send_data_saved
    "POLL",		// send_poll
    "PAGE",		// send_page
    "PAGE",		// send_page_saved
    "UUCP",		// send_uucp
    "UNKNOWN",		// send_unknown
};

static const char*
boolString(bool b)
{
    return (b ? "YES" : "NO");
}
void
HylaFAXServer::replyBoolean(int code, bool b)
{
    reply(code, "%s", boolString(b));
}

/*
 * Check that the job's in-memory state is consistent
 * with what is on disk.  If the on-disk state is newer then
 * read it in.  If the job has been removed then remove our
 * reference, reset the current job back to the default job,
 * and send the client an error reply.  This method is used
 * before each place a job's state is access.
 */
bool
HylaFAXServer::checkJobState(Job* job)
{
    /*
     * Verify job is still around (another process has
     * not deleted it) and if the on-disk state has
     * been updated, re-read the job description file.
     */
    struct stat sb;
    if (!FileCache::update("/" | job->qfile, sb)) {
	jobs.remove(job->jobid);
	if (job == curJob) {			// make default job current
	    curJob = &defJob;
	    curJobId = "default";
	    curJobGroupId = "";
	}
	delete job, job = NULL;
	return (false);
    }
    if (job->lastmod < sb.st_mtime) {
	if (updateJobFromDisk(*job))
		job->lastmod = sb.st_mtime;
    }
    return (true);
}

/*
 * Check if it's ok to do the specified operation on the
 * current job's state parameter.  If not, return the appropriate
 * error reply.   We disallow modifications on jobs that
 * do not appear to be suspended since otherwise the mods
 * could be lost (in our case they will be lost due to the
 * way that things work).
 */
bool
HylaFAXServer::checkParm(Job& job, Token t, u_int op)
{
    if (!checkJobState(&job)) {			// insure consistent state
	reply(500, "Cannot access job state; job deleted by another party.");
	return (false);
    } else if (!checkAccess(job, t, op)) {
	reply(503, "Permission denied: no %s access to job parameter %s."
	    , (op == A_READ ? "read" : "write")
	    , parmToken(t)
	);
	return (false);
    } else if ((op & (A_WRITE|A_MODIFY)) &&
      job.state != FaxRequest::state_suspended && job.jobid != "default") {
	reply(503, "Suspend the job with JSUSP first.");
	return (false);
    } else
	return (true);
}

/*
 * Respond to a job state parameter query.
 */
void
HylaFAXServer::replyJobParamValue(Job& job, int code, Token t)
{
    if (!checkParm(job, t, A_READ))
	return;
    u_int i, n;
    switch (t) {
    case T_SENDTIME:
	if (job.tts != 0) {
	    const struct tm* tm = cvtTime(job.tts);
	    // XXX should this include seconds?
	    reply(code, "%4d%02d%02d%02d%02d%02d"
		, tm->tm_year+1900
		, tm->tm_mon+1
		, tm->tm_mday
		, tm->tm_hour
		, tm->tm_min
		, tm->tm_sec
	    );
	} else
	    reply(code, "NOW");
	return;
    case T_LASTTIME:
	time_t tv; tv = job.killtime - job.tts;		// XXX for __GNUC__
	reply(code, "%02d%02d%02d", tv/(24*60*60), (tv/(60*60))%24, (tv/60)%60);
	return;
    case T_RETRYTIME:
	reply(code, "%02d%02d", job.retrytime/60, job.retrytime%60);
	return;
    case T_STATE:
	reply(code, "%s", stateVals[job.state]);
	return;
    case T_NOTIFY:
	reply(code, "%s", notifyVals[job.notify]);
	return;
    case T_PAGECHOP:
	reply(code, "%s", chopVals[job.pagechop]);
	return;
    case T_CHOPTHRESH:
	reply(code, "%g", job.chopthreshold);
	return;
    case T_DATAFORMAT:
	reply(code, "%s", dataVals[job.desireddf]);
	return;
    case T_ECMTYPE:
	reply(code, "%s", ecmVals[job.desiredec]);
	return;
    case T_USE_ECM:
	replyBoolean(code, (job.desiredec != EC_DISABLE ? 1 : 0));
	return;
    case T_USE_TAGLINE:
	replyBoolean(code, job.desiredtl);
	return;
    case T_USE_XVRES:
	replyBoolean(code, job.usexvres);
	return;
    case T_USE_COLOR:
	replyBoolean(code, job.usecolor);
	return;
    case T_USE_CONTCOVER:
	replyBoolean(code, job.useccover);
	return;
    case T_SERVERDOCOVER:
	replyBoolean(code, job.serverdocover);
	return;
    case T_IGNOREMODEMBUSY:
	replyBoolean(code, job.ignoremodembusy);
	return;
    case T_DOCUMENT:
	for (i = 0, n = job.items.length(); i < n; i++) {
	    const FaxItem& fitem = job.items[i];
	    // XXX should cover page docs not be shown?
	    switch (fitem.op) {
	    case FaxRequest::send_pdf:
	    case FaxRequest::send_pdf_saved:
	    case FaxRequest::send_tiff:
	    case FaxRequest::send_tiff_saved:
	    case FaxRequest::send_postscript:
	    case FaxRequest::send_postscript_saved:
	    case FaxRequest::send_pcl:
	    case FaxRequest::send_pcl_saved:
		lreply(code, "%s %s",
		    docTypeNames[fitem.op], (const char*) fitem.item);
		break;
	    }
	}
	reply(code, "End of documents.");
	return;
    case T_COVER:
	for (i = 0, n = job.items.length(); i < n; i++) {
	    const FaxItem& fitem = job.items[i];
	    if (fitem.item.length() > 7 && fitem.item.tail(6) == ".cover") {
		switch (fitem.op) {
		case FaxRequest::send_tiff:
		case FaxRequest::send_tiff_saved:
		case FaxRequest::send_pdf:
		case FaxRequest::send_pdf_saved:
		case FaxRequest::send_postscript:
		case FaxRequest::send_postscript_saved:
		case FaxRequest::send_pcl:
		case FaxRequest::send_pcl_saved:
		    reply(code, "%s %s",
			docTypeNames[fitem.op], (const char*) fitem.item);
		    return;
		}
	    }
	}
	reply(code+1, "No cover page document.");
	return;
    case T_POLL:
	for (i = 0, n = job.items.length(); i < n; i++) {
	    const FaxItem& fitem = job.items[i];
	    if (fitem.op == FaxRequest::send_poll)
		lreply(code, "\"%s\" \"%s\"",
		    (const char*) fitem.item, (const char*) fitem.addr);
	}
	reply(code, "End of polling items.");
	return;
    case T_JOBID:
    case T_GROUPID:
	for (i = 0, n = N(strvals); i < n; i++)
	    if (strvals[i].t == t) {
		reply(code, "%s%s", job.jobid == "default" ? "" : (const char*) jobHostId, (const char*) (job.*strvals[i].p));
		return;
	    }
	return;
    default:
	break;
    }
    for (i = 0, n = N(strvals); i < n; i++)
	if (strvals[i].t == t) {
	    reply(code, "%s", (const char*) (job.*strvals[i].p));
	    return;
	}
    for (i = 0, n = N(shortvals); i < n; i++)
	if (shortvals[i].t == t) {
	    reply(code, "%u", job.*shortvals[i].p);
	    return;
	}
    for (i = 0, n = N(intvals); i < n; i++)
	if (intvals[i].t == t) {
	    reply(code, "%u", job.*intvals[i].p);
	    return;
	}
    reply(500, "Botch: no support for querying parameter value.");
}

void
HylaFAXServer::jstatLine(Token t, const char* fmt ...)
{
    printf("    %s: ", parmToken(t));
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
}

/*
 * Implement the jparm command that returns the entire
 * job state as a series of parameter: value pairs.
 * This command is mainly intended for debugging and
 * may go away in the final spec (or become a site cmd).
 */
void
HylaFAXServer::jstatCmd(const Job& job)
{
    lreply(217, "Job state: jobid %s%s groupid %s%s",
	job.jobid == "default" ? "" : (const char*) jobHostId, (const char*) job.jobid, job.jobid == "default" ? "" : (const char*) jobHostId, (const char*) job.groupid);
    if (checkAccess(job, T_SENDTIME, A_READ)) {
	if (job.tts != 0) {
	    const struct tm* tm = cvtTime(job.tts);
	    // XXX should this include seconds? (useful for debugging)
	    jstatLine(T_SENDTIME, "%d%02d%02d%02d%02d%02d"
		, tm->tm_year+1900
		, tm->tm_mon+1
		, tm->tm_mday
		, tm->tm_hour
		, tm->tm_min
		, tm->tm_sec
	    );
	} else
	    jstatLine(T_SENDTIME, "%s", "NOW");
    }
    if (checkAccess(job, T_LASTTIME, A_READ)) {
	time_t tv = job.killtime - job.tts;
	jstatLine(T_LASTTIME, "%02d%02d%02d",
	    tv/(24*60*60), (tv/(60*60))%24, (tv/60)%60);
    }
    if (checkAccess(job, T_RETRYTIME, A_READ))
	jstatLine(T_RETRYTIME, "%02d%02d", job.retrytime/60, job.retrytime%60);
    if (checkAccess(job, T_STATE, A_READ))
	jstatLine(T_STATE,	"%s", stateVals[job.state]);
    if (checkAccess(job, T_NOTIFY, A_READ))
	jstatLine(T_NOTIFY,	"%s", notifyVals[job.notify]);
    if (checkAccess(job, T_PAGECHOP, A_READ))
	jstatLine(T_PAGECHOP,	"%s", chopVals[job.pagechop]);
    if (checkAccess(job, T_CHOPTHRESH, A_READ))
	jstatLine(T_CHOPTHRESH,	"%g", job.chopthreshold);
    if (checkAccess(job, T_DATAFORMAT, A_READ))
	jstatLine(T_DATAFORMAT,	"%s", dataVals[job.desireddf]);
    if (checkAccess(job, T_ECMTYPE, A_READ))
	jstatLine(T_ECMTYPE,	"%s", ecmVals[job.desiredec]);
    if (checkAccess(job, T_USE_ECM, A_READ))
	jstatLine(T_USE_ECM,	"%s", boolString(job.desiredec != EC_DISABLE ? 1 : 0));
    if (checkAccess(job, T_USE_TAGLINE, A_READ))
	jstatLine(T_USE_TAGLINE,"%s", boolString(job.desiredtl));
    if (checkAccess(job, T_USE_XVRES, A_READ))
	jstatLine(T_USE_XVRES,"%s", boolString(job.usexvres));
    if (checkAccess(job, T_USE_COLOR, A_READ))
	jstatLine(T_USE_COLOR,"%s", boolString(job.usecolor));
    if (checkAccess(job, T_USE_CONTCOVER, A_READ))
	jstatLine(T_USE_CONTCOVER,"%s", boolString(job.useccover));
    if (checkAccess(job, T_SERVERDOCOVER, A_READ))
	jstatLine(T_SERVERDOCOVER,"%s", boolString(job.serverdocover));
    if (checkAccess(job, T_IGNOREMODEMBUSY, A_READ))
	jstatLine(T_IGNOREMODEMBUSY,"%s", boolString(job.ignoremodembusy));
    u_int i, n;
    for (i = 0, n = N(strvals); i < n; i++)
	if (checkAccess(job, strvals[i].t, A_READ)) {
	    if ((strvals[i].t == T_JOBID || strvals[i].t == T_GROUPID) && job.jobid != "default")
		jstatLine(strvals[i].t, "%s%s", (const char*) jobHostId, (const char*) (job.*strvals[i].p));
	    else
		jstatLine(strvals[i].t, "%s", (const char*) (job.*strvals[i].p));
	}
    for (i = 0, n = N(shortvals); i < n; i++)
	if (checkAccess(job, shortvals[i].t, A_READ))
	    jstatLine(shortvals[i].t, "%u", job.*shortvals[i].p);
    for (i = 0, n = N(intvals); i < n; i++)
	if (checkAccess(job, intvals[i].t, A_READ))
	    jstatLine(intvals[i].t, "%u", job.*intvals[i].p);
    /*
     * NB: This assumes access to T_DOCUMENT is sufficient
     *     for access to T_COVER and T_POLL also.
     */
    if (checkAccess(job, T_DOCUMENT, A_READ)) {
	for (i = 0, n = job.items.length(); i < n; i++) {
	    const FaxItem& fitem = job.items[i];
	    switch (fitem.op) {
	    case FaxRequest::send_fax:
		jstatLine(T_DOCUMENT, "%s %s %u", docTypeNames[fitem.op],
		    (const char*) fitem.item, fitem.dirnum);
		break;
	    case FaxRequest::send_tiff:
	    case FaxRequest::send_tiff_saved:
	    case FaxRequest::send_pdf:
	    case FaxRequest::send_pdf_saved:
	    case FaxRequest::send_postscript:
	    case FaxRequest::send_postscript_saved:
	    case FaxRequest::send_pcl:
	    case FaxRequest::send_pcl_saved:
	    case FaxRequest::send_data:
	    case FaxRequest::send_data_saved:
		jstatLine(
		    (fitem.item.length() > 7 && fitem.item.tail(6) == ".cover" ?
			T_COVER : T_DOCUMENT) 
		    , "%s %s"
		    , docTypeNames[fitem.op]
		    , (const char*) fitem.item
		);
		break;
	    case FaxRequest::send_page:
	    case FaxRequest::send_page_saved:
		jstatLine(T_DOCUMENT
		    , fitem.addr == "" ? "%s \"%s\"" : "%s \"%s\" \"%s\""
		    , docTypeNames[fitem.op]
		    , (const char*) fitem.item
		    , (const char*) fitem.addr
		);
		break;
	    case FaxRequest::send_poll:
		jstatLine(T_POLL
		    , fitem.addr == "" ? "\"%s\"" : "\"%s\" \"%s\""
		    , (const char*) fitem.item
		    , (const char*) fitem.addr
		);
		break;
	    }
	}
    }
    reply(217, "End of job %s%s state.", job.jobid == "default" ? "" : (const char*) jobHostId, (const char*) job.jobid);
}

/* 
 * Set a job state parameter value from an array of
 * parameter names; the value is the index into the
 * array.  Parameter names are case-insensitive. 
 * Unknown values cause an error reply.  
 */
bool
HylaFAXServer::setValue(u_short& v, const char* value, const char* what,
    const char* valNames[], u_int nValNames)
{
    for (u_int i = 0; i < nValNames; i++)
	if (strcasecmp(value, valNames[i]) == 0) {
	    v = i;
	    return (true);
	}
    reply(503, "Unknown %s value %s.", what, value);
    return (false);
}

void
HylaFAXServer::parmBotch(Token t)
{
    reply(503, "Botch, don't know how to set %s parameter.", parmToken(t));
}

/*
 * Discard any prepared documents so that
 * they will be re-prepared with revised
 * parameters.
 */
void
HylaFAXServer::flushPreparedDocuments(Job& job)
{
    u_int j = 0;
    while (j < job.items.length()) {
	FaxItem& fitem = job.items[j];
	if (fitem.op == FaxRequest::send_fax) {
	    // NB: don't waste time requesting ACK
	    fxStr emsg;
	    sendQueuer(emsg, "U%s", (const char*) fitem.item);
	    job.items.remove(j);
	    continue;
	}
	if (fitem.isSavedOp())
	    fitem.op--;				// assumes order of enum
	j++;
    }
    job.pagehandling = "";			// force recalculation
}

/*
 * Set a job state parameter that has a string value.
 */
bool
HylaFAXServer::setJobParameter(Job& job, Token t, const fxStr& value)
{
    if (checkParm(job, t, A_WRITE|A_MODIFY)) {
	switch (t) {
	case T_NOTIFY:
	    return setValue(job.notify, value, parmToken(t),
		notifyVals, N(notifyVals));
	case T_PAGECHOP:
	    if (setValue(job.pagechop, value, parmToken(t),
	      chopVals, N(chopVals))) {
		job.pagehandling = "";		// force recalculation
		return (true);
	    } else
		return (false);
	case T_DATAFORMAT:
	    if (setValue(job.desireddf, value, parmToken(t),
	      dataVals, N(dataVals))) {
		flushPreparedDocuments(job);
		return (true);
	    } else
		return (false);
	case T_ECMTYPE:
	    if (setValue(job.desiredec, value, parmToken(t),
	      ecmVals, N(ecmVals))) {
		return (true);
	    } else
		return (false);
	default:
	    break;
	}
	for (u_int i = 0, n = N(strvals); i < n; i++)
	    if (strvals[i].t == t) {
		job.*strvals[i].p = value;
		return (true);
	    }
	parmBotch(t);
    }
    return (false);
}

/*
 * Set a job state parameter that has a integer value.
 */
bool
HylaFAXServer::setJobParameter(Job& job, Token t, u_short value)
{
    if (checkParm(job, t, A_WRITE|A_MODIFY)) {
	for (u_int i = 0, n = N(shortvals); i < n; i++)
	    if (shortvals[i].t == t) {
		if (job.*shortvals[i].p != value) {
		    // XXX constrain values per A_MODIFY
		    job.*shortvals[i].p = value;	// XXX
		    /*
		     * Handle parameters with side effects.
		     */
		    switch (t) {
		    case T_PAGEWIDTH:
		    case T_PAGELENGTH:
		    case T_VRES:
		    case T_HRES:
			flushPreparedDocuments(job);
			break;
		    case T_SCHEDPRI:
			job.pri = job.usrpri;		// reload
			break;
		    default:
			break;
		    }
		}
		return (true);
	    }
	for (u_int i = 0, n = N(intvals); i < n; i++)
	    if (intvals[i].t == t) {
		if (job.*intvals[i].p != value) {
		    // XXX constrain values per A_MODIFY
		    job.*intvals[i].p = value;	// XXX
		}
		return (true);
	    }
	parmBotch(t);
    }
    return (false);
}

/*
 * Set a job state parameter that has a time value.
 */
bool
HylaFAXServer::setJobParameter(Job& job, Token t, time_t value)
{
    if (checkParm(job, t, A_WRITE|A_MODIFY)) {
	time_t now = Sys::now();
	switch (t) {
	case T_SENDTIME:
	    if (value != 0) {			// explicit time
		/*
		 * We don't complain anymore if this value is in the
		 * past.  Instead, we verify that the killtime is in
		 * the future, ensuring that a window of send-time
		 * opportunity still exists.
		 */
		job.tts = value;
	    } else				// ``NOW''
		job.tts = now;
	    return (true);
	case T_LASTTIME:
	    /*
	     * Convert client-specified kill time (as a relative number)
	     * to an absolute time.  If the time-to-send has been set,
	     * then the killtime is relative to that; otherwise it's
	     * made relative to ``now''.  Note that this implies the
	     * order of setting SENDTIME and LASTTIME is important; if
	     * a client sets LASTTIME before SENDTIME then an unexpected
	     * value may be installed for LASTTIME.
	     */
	    job.killtime = value + (job.tts == 0 ? now : job.tts);
	    if (job.killtime < now) {
		reply(503, "Bad time to send; time window is entirely in the past.");
		return (false);
	    }
	    return (true);
	case T_RETRYTIME:
	    job.retrytime = value;
	    return (true);
	default:
	    break;
	}
	parmBotch(t);
    }
    return (false);
}

/*
 * Set a job state parameter that has a boolean value.
 */
bool
HylaFAXServer::setJobParameter(Job& job, Token t, bool b)
{
    if (checkParm(job, t, A_WRITE|A_MODIFY)) {
	switch (t) {
	case T_USE_ECM:
	    job.desiredec = (b ? (job.desiredec == EC_DISABLE ? EC_ENABLE256 : job.desiredec) : EC_DISABLE);
	    return (true);
	case T_USE_TAGLINE:
	    job.desiredtl = b;
	    return (true);
	case T_USE_XVRES:
	    job.usexvres = b;
	    return (true);
	case T_USE_COLOR:
	    job.usecolor = b;
	    return (true);
	case T_USE_CONTCOVER:
	    job.useccover = b;
	    return (true);
	case T_SERVERDOCOVER:
	    job.serverdocover = b;
	    return (true);
	case T_IGNOREMODEMBUSY:
	    job.ignoremodembusy = b;
	    return (true);
	default:
	    break;
	}
	parmBotch(t);
    }
    return (false);
}

/*
 * Set a job state parameter that has a float value.
 */
bool
HylaFAXServer::setJobParameter(Job& job, Token t, float value)
{
    if (checkParm(job, t, A_WRITE|A_MODIFY)) {
	switch (t) {
	case T_CHOPTHRESH:
	    if (job.chopthreshold != value) {
		job.chopthreshold = value;
		job.pagehandling = "";		// force recalculation
	    }
	    return (true);
	default:
	    break;
	}
	parmBotch(t);
    }
    return (false);
}

/*
 * Initialize the default job state.  We
 * explicitly set string values since this
 * routine may be called for a JREST command
 * to reset job state to the default settings.
 */
void
HylaFAXServer::initDefaultJob(void)
{
    defJob.jobid	= "default";
    defJob.owner	= the_user;
    defJob.state	= FaxRequest::state_undefined;
    defJob.maxdials	= FAX_REDIALS;
    defJob.maxtries	= FAX_RETRIES;
    defJob.pagewidth	= 0;
    defJob.pagelength	= 0;
    defJob.resolution	= FAX_DEFVRES;
    defJob.usrpri	= FAX_DEFPRIORITY;
    defJob.minbr	= BR_2400;
    defJob.desiredbr	= BR_33600;
    defJob.desiredst	= ST_0MS;
    defJob.desiredec	= EC_ENABLE256;
    defJob.desireddf	= DF_2DMMR;
    defJob.desiredtl	= false;
    defJob.usexvres	= false;
    defJob.usecolor	= false;
    defJob.useccover	= true;
    defJob.serverdocover= false;
    defJob.ignoremodembusy= false;
    defJob.pagechop	= FaxRequest::chop_default;
    defJob.notify	= FaxRequest::no_notice;// FAX_DEFNOTIFY
    defJob.chopthreshold= 3.0;
    defJob.tts		= 0;			// ``NOW''
    defJob.killtime	= 3*60*60;		// FAX_TIMEOUT
    defJob.retrytime	= 0;
    defJob.sender	= the_user;		// XXX usually incorrect
    defJob.mailaddr	= the_user | "@" | remotehost;
    defJob.jobtag	= "";
    defJob.number	= "";
    defJob.subaddr	= "";
    defJob.passwd	= "";
    defJob.external	= "";
    defJob.modem	= MODEM_ANY;
    defJob.faxnumber	= "";
    defJob.faxname	= "";
    defJob.tsi		= "";
    defJob.receiver	= "";
    defJob.company	= "";
    defJob.location	= "";
    defJob.voice	= "";
    defJob.fromcompany	= "";
    defJob.fromlocation	= "";
    defJob.fromvoice	= "";
    defJob.regarding	= "";
    defJob.comments	= "";
    defJob.timeofday	= "";
    defJob.timezone	= "";
    defJob.errorcode	= "";
    defJob.client	= remotehost;
    defJob.tagline	= "";
    defJob.doneop	= "default";
    defJob.nocountcover	= 0;
    defJob.probeonly	= 0;
    defJob.conntime	= 0;
    defJob.duration	= 0;
    defJob.skippedpages	= 0;
    defJob.skippages	= 0;
}

/*
 * JNEW command; create a new job and
 * make it the current job.
 */
void
HylaFAXServer::newJobCmd(void)
{
    fxStr emsg;
    if (newJob(emsg) && updateJobOnDisk(*curJob, emsg)) {
	fxStr file("/" | curJob->qfile);
	setFileOwner(file);			// force ownership
	FileCache::chmod(file, jobProtection);	// sync cache
	curJob->lastmod = Sys::now();		// noone else should update
	reply(200, "New job created: jobid: %s groupid: %s.",
	    (const char*) curJobId, (const char*) curJobGroupId);
	blankJobs[curJob->jobid] = curJob;
    } else
	reply(503, "%s.", (const char*) emsg);
}

/*
 * Create a new job, inheriting state from the current
 * job and make the new job be the current job.  Note
 * that the current job must be owned by the client;
 * otherwise people could ``look inside'' other people's
 * jobs by inheriting state--this would permit them to
 * look at privileged information such as calling card
 * information in dial strings and passwords to be
 * transmitted with polling items.
 */
bool
HylaFAXServer::newJob(fxStr& emsg)
{
    if (!IS(PRIVILEGED) && the_user != curJob->owner) {
	emsg = "Permission denied; cannot inherit from job " | jobHostId | curJob->jobid;
	return (false);
    }
    u_int id = getJobNumber(emsg);		// allocate unique job ID
    if (id == (u_int) -1)
	return (false);
    fxStr jobid = fxStr::format("%u", id);
    Job* job = new Job(FAX_SENDDIR "/" FAX_QFILEPREF | jobid);
    job->jobid = jobid;
    job->groupid = curJob->groupid;
    if (job->groupid == "")
	job->groupid = jobid;
    job->owner = the_user;
    job->state = FaxRequest::state_suspended;
    job->maxdials = curJob->maxdials;
    job->maxtries = curJob->maxtries;
    job->pagewidth = curJob->pagewidth;
    job->pagelength = curJob->pagelength;
    job->resolution = curJob->resolution;
    job->usrpri = curJob->usrpri;
    job->minbr = curJob->minbr;
    job->desiredbr = curJob->desiredbr;
    job->desiredst = curJob->desiredst;
    job->desiredec = curJob->desiredec;
    job->desireddf = curJob->desireddf;
    job->desiredtl = curJob->desiredtl;
    job->usexvres = curJob->usexvres;
    job->usecolor = curJob->usecolor;
    job->useccover = curJob->useccover;
    job->serverdocover = curJob->serverdocover;
    job->ignoremodembusy = curJob->ignoremodembusy;
    job->pagechop = curJob->pagechop;
    job->notify = curJob->notify;
    job->chopthreshold = curJob->chopthreshold;
    job->tts = curJob->tts;
    job->killtime = curJob->killtime;
    job->retrytime = curJob->retrytime;
    job->sender = curJob->sender;
    job->mailaddr = curJob->mailaddr;
    job->jobtag = curJob->jobtag;		// ???
    job->number = curJob->number;
    job->external = curJob->external;
    job->modem = curJob->modem;
    job->faxnumber = curJob->faxnumber;
    job->faxname = curJob->faxname;
    job->tsi = curJob->tsi;
    job->receiver = curJob->receiver;
    job->company = curJob->company;
    job->location = curJob->location;
    job->voice = curJob->voice;
    job->fromcompany = curJob->fromcompany;
    job->fromlocation = curJob->fromlocation;
    job->fromvoice = curJob->fromvoice;
    job->regarding = curJob->regarding;
    job->comments = curJob->comments;
    job->timeofday = curJob->timeofday;
    job->timezone = curJob->timezone;
    job->jobtype = curJob->jobtype;
    job->tagline = curJob->tagline;
    job->client = remotehost;
    job->doneop = curJob->doneop;
    job->queued = curJob->queued;
    jobs[jobid] = job;
    curJob = job;
    curJobId = jobHostId | job->jobid;
    curJobGroupId = jobHostId | job->groupid;
    return (true);
}

/*
 * Update the job's state on disk.
 */
bool
HylaFAXServer::updateJobOnDisk(Job& job, fxStr& emsg)
{
    if (job.fd < 0)
    {
	job.fd = Sys::open("/" | job.qfile, O_RDWR|O_CREAT, jobProtection);
	if (job.fd < 0)
	{
	    emsg = "Cannot open/create job description file /" | job.qfile;
	    return false;
	}
    }

    if (lockJob(job, LOCK_EX, emsg)) {
	// XXX don't update in place, use temp file and rename
	job.writeQFile();
	unlockJob(job);
	return (true);
    } else
	return (false);
}

/*
 * Look for a job in the in-memory cache.
 */
Job*
HylaFAXServer::findJobInMemory(const char* jobid)
{
    /* jobid here does not include jobHostId */

    if (curJob->jobid == jobid)				// fast check
	return (curJob);
    Job** jpp = (Job**) jobs.find(jobid);
    if (jpp)
	return (*jpp);
    return (jobid == defJob.jobid ? &defJob : (Job*) NULL);
}

/*
 * Look for a job on disk and, if found, read it
 * into memory and return it.
 */
Job*
HylaFAXServer::findJobOnDisk(const char* jobid, fxStr& emsg)
{
    /* jobid here does not include jobHostId */

    fxStr filename(fxStr::format("/" FAX_SENDDIR "/" FAX_QFILEPREF "%s", jobid));
    struct stat sb;
    if (!FileCache::update(filename, sb)) {
	/*
	 * Not in sendq, look in the doneq for the job.
	 */
	filename = fxStr::format("/" FAX_DONEDIR "/" FAX_QFILEPREF "%s", jobid);
	if (!FileCache::update(filename, sb)) {
	    emsg = fxStr::format("job does not exist (%s)", strerror(errno));
	    return (NULL);
	}
    }
    if (!S_ISREG(sb.st_mode)) {
	emsg = "job description file is not a regular file";
	return (NULL);
    }
    int fd = Sys::open(filename, O_RDWR);
    if (fd >= 0) {
	// XXX should we lock here???
	Job* req = new Job(&filename[1], fd);
	bool reject;
	if (req->readQFile(reject) && !reject) {
	    Sys::close(req->fd), req->fd = -1;
	    if (checkAccess(*req, T_JOB, A_READ) )
		return (req);
	    emsg = "Permission denied";
	    delete req;
	    return (NULL);
	}
	emsg = "invalid or corrupted job description file";
	delete req;			// NB: closes fd
    } else
	emsg = fxStr::format("cannot open job description file %s (%s)",
	    (const char*) filename, strerror(errno));
    return (NULL);
}

/*
 * Update a job's state from the on-disk copy.
 */
bool
HylaFAXServer::updateJobFromDisk(Job& job)
{
    bool status = false;
    if (lockJob(job, LOCK_SH)) {
	bool reject;
	status = (job.reReadQFile(reject) && !reject);
	unlockJob(job);
    }
    return (status);
}

/*
 * Lock a job description file, creating it if
 * necessary.  Errors are not expected--if one
 * occurs a descriptive message is returned for
 * transmission to the client.
 */
bool
HylaFAXServer::lockJob(Job& job, int how, fxStr& emsg)
{
    if (job.fd < 0) {
	job.fd = Sys::open("/" | job.qfile, O_RDWR, jobProtection);
	if (job.fd < 0) {
	    emsg = "Cannot open/create job description file /" | job.qfile;
	    return (false);
	}
    }
    if (flock(job.fd, how | LOCK_NB) >= 0)
	return true;

    if (errno == EWOULDBLOCK && lockTimeout > 0)
    {
	int r;
	Timeout timer;

	timer.startTimeout(lockTimeout*1000);
	r = flock(job.fd, how);
	timer.stopTimeout();

	if (timer.wasTimeout())
	    logDebug("LOCKWAIT timeout: %ds", lockTimeout);

	return (r >= 0);
    }

    emsg = fxStr::format("Job file lock failed: %s", strerror(errno));
    Sys::close(job.fd);
    job.fd = -1;
    return (false);
}


/*
 * Like above, but no error message is returned.
 */
bool
HylaFAXServer::lockJob(Job& job, int how)
{
    if (job.fd < 0)
    {
	job.fd = Sys::open("/" | job.qfile, O_RDWR, jobProtection);
	if (job.fd < 0)
	    return false;
    }
    if (flock(job.fd, how | LOCK_NB) >= 0)
	return true;

    if (errno == EWOULDBLOCK && lockTimeout > 0)
    {
	int r;
	Timeout timer;

	timer.startTimeout(lockTimeout * 1000);
	r = flock(job.fd, how);
	timer.stopTimeout();

	if (timer.wasTimeout())
	    logDebug("LOCKWAIT timeout: %ds", lockTimeout);

	return (r >= 0);
    }

    return (false);
}

/*
 * Unlock a previously-locked job.
 */
void
HylaFAXServer::unlockJob(Job& job)
{
    if (job.fd >= 0)
	Sys::close(job.fd), job.fd = -1;	// implicit unlock
}

/*
 * Find a job either in memory (in the cache) or
 * on disk.  If a job is found in memory and the
 * on-disk state is more current, update the state
 * in the cache.
 */
Job*
HylaFAXServer::findJob(const char* jobid, fxStr& emsg)
{
    /* jobid here does not include jobHostId */

    Job* job = findJobInMemory(jobid);
    if (job) {
	/*
	 * Verify job is still around (another process has
	 * not deleted it) and if the on-disk state has
	 * been updated, re-read the job description file.
	 */
	if (!checkJobState(job)) {
            // We will re-check on disk in case a job was moved between queues
            job = NULL;
	    emsg = "job deleted by another party";
        }
    } 
    if (!job) {
	/*
	 * We can only afford a certain amount of space,
	 * unfortunately, there is no "bright" way to remove jobs
	 * Ideally we'ld have an "aging" method, so the LRU job
	 * would be the one deleted...
	 */
	if (jobs.size() > 10)
	{
	    JobDictIter iter(jobs);
	    job = iter.value();
	    jobs.remove(job->jobid);
	    delete job;
	}

	job = findJobOnDisk(jobid, emsg);
	if (job)
	    jobs[job->jobid] = job;
    }
    return (job);
}

/*
 * Purge all in-memory job state.
 */
void
HylaFAXServer::purgeJobs(void)
{
    for (JobDictIter iter(jobs); iter.notDone(); iter++) {
	Job* job = iter.value();
	jobs.remove(job->jobid);
	delete job;
    }
}

/*
 * Send a reply identifying the current job.
 */
void
HylaFAXServer::replyCurrentJob(const char* leader)
{
    if (curJobId == "default")
	reply(200, "%s (default).", leader);
    else
	reply(200, "%s jobid: %s groupid: %s.", leader,
	    (const char*) curJobId, (const char*) curJobGroupId);
}

/*
 * Establish job-host login.
 */
bool
HylaFAXServer::setupJobHost(const char* jobid, fxStr& emsg)
{
    for (int i = 0; i < jobHosts.length(); i++) {
	if (strncmp(jobid, (const char*) jobHosts[i].id, jobHosts[i].id.length()) == 0) {
	    if (curJobHost != i) {
		// We're switching remote host connections.  We need to disconnect 
		// from the current (if there is) and connect to the new.
		if (curJobHost != -1) {
		    // There is a current connection.  Disconnect it first.
		    jobHostClient->hangupServer();
		}
		jobHostClient->readConfig(FAX_SYSCONF);
		jobHostClient->setHost(jobHosts[i].host);
		if (jobHostClient->callServer(emsg)) {
		    if (jobHostClient->login(jobHosts[i].user.length() ? (const char*) jobHosts[i].user : (const char*) the_user, jobHosts[i].user.length() ? (const char*) jobHosts[i].pass : (const char*) usrPassWd, emsg)) {
			if (jobHosts[i].user.length()) {
			    if (jobHosts[i].adminpw != "*" && !jobHostClient->admin((const char*) jobHosts[i].adminpw, emsg)) {
				emsg = "administrative privileges failed with job host (configured)";
				return (false);
			    }
			} else {
			    if (usrAdminWd != "*" && !jobHostClient->admin((const char*) usrAdminWd, emsg)) {
				emsg = "administrative privileges failed with job host (supplied)";
				return (false);
			    }
			}
			curJobHost = i;
			return (true);
		    } else emsg = "could not authenticate with job host";
		} else emsg = "could not connect to job host";
		return (false);
	    } else {
		// Connection can be re-used.
		return (true);
	    }
	}
    }
    // The client appears to be attempting to work with a job on an unknown host.
    emsg = "unknown host id";
    return (false);
}

/*
 * Set the current job.
 */
void
HylaFAXServer::setCurrentJob(const char* jobid)
{
    fxStr emsg;
    if (strlen(jobid) > jobHostId.length()) {
	bool isdefault = (strncmp(jobid, "default", 7) == 0);
	if (jobHostId.length() == 0 || isdefault || strncmp(jobid, (const char*) jobHostId, jobHostId.length()) == 0) {
	    // This appears to be a job on this host.
	    Job* job = findJob(jobid + (isdefault ? 0 : jobHostId.length()), emsg);
	    if (job) {
		curJob = job;
		curJobId = (curJob->jobid == "default" ? "" : jobHostId) | curJob->jobid;
		curJobGroupId = (curJob->jobid == "default" ? "" : jobHostId) | curJob->groupid;
		replyCurrentJob("Current job:");
		return;
	    }
	} else {
	    if (setupJobHost(jobid, emsg)) {
		jobHostClient->command((const char*) fxStr::format("JOB %s", jobid));
		fxStr r = jobHostClient->getLastResponse();
		// Extract curJobId and curJobGroupId from response.  Format is:
		// "200 Current job: jobid: XXXX groupid: XXXX."
		u_int jp = r.find(0, " jobid: ");
		u_int gp = r.find(jp < r.length()-8 ? jp+8 : 0, " groupid: ");
		if (gp-jp > 8 && r.length()-gp > 11) {
		    curJobId = r.extract(jp+8, gp-jp-8);
		    curJobGroupId = r.extract(gp+10, r.length()-gp-11);
		}
		reply(-1, (const char*) r);
		return;
	    }
	}
    } else {
	emsg = "missing host id";
    }
    reply(500, "Cannot set job %s; %s.", jobid, (const char*) emsg);
}

/*
 * Reset a job's state to what is currently on disk.
 */
void
HylaFAXServer::resetJob(const char* jobid)
{
    fxStr emsg;
    if (strlen(jobid) > jobHostId.length()) {
	bool isdefault = (strncmp(jobid, "default", 7) == 0);
	if (jobHostId.length() == 0 || isdefault || strncmp(jobid, (const char*) jobHostId, jobHostId.length()) == 0) {
	    // This appears to be a job on this host.
	    Job* job = findJob(jobid + (isdefault ? 0 : jobHostId.length()), emsg);
	    if (job) {
		if (job->jobid == "default") {
		    initDefaultJob();
		    reply(200, "Default job reset to initial state.");
		} else if (job->state != FaxRequest::state_suspended) {
		    reply(504, "Job %s not reset; must be suspended.", jobid);
		} else {
		    updateJobFromDisk(*job);
		    struct stat sb;
		    if (FileCache::lookup("/" | job->qfile, sb))
			job->lastmod = sb.st_mtime;
		    reply(200, "Job %s reset to last state saved to disk.", jobid);
		}
		return;
	    }
	} else {
	    if (setupJobHost(jobid, emsg)) {
		jobHostClient->command((const char*) fxStr::format("JREST %s", jobid));
		reply(-1, (const char*) jobHostClient->getLastResponse());
		return;
	    }
	}
    } else {
	emsg = "missing host id";
    }
    reply(500, "Cannot reset job %s; %s.", jobid, (const char*) emsg);
}

/*
 * Common work done for many job-related commands.
 */
Job*
HylaFAXServer::preJobCmd(const char* op, const char* jobid, fxStr& emsg)
{
    /* jobid here does not include jobHostId */

    Job* job = findJob(jobid, emsg);
    if (job) {
	if (job->jobid == "default") {
	    reply(504, "Cannot %s default job.", op);
	    job = NULL;
	} else if (!IS(PRIVILEGED) && job->owner != the_user) {
	    reply(504, "Cannot %s job: %s.", op, strerror(EPERM));
	    job = NULL;
	}
    } else
	reply(500, "Cannot %s job %s%s; %s.", op, (const char*) jobHostId, jobid, (const char*) emsg);
    return (job);
}

/*
 * Delete all job state (both on disk and in memory).
 */
void
HylaFAXServer::deleteJob(const char* jobid)
{
    fxStr emsg;
    if (strlen(jobid) > jobHostId.length()) {
	bool isdefault = (strncmp(jobid, "default", 7) == 0);
	if (jobHostId.length() == 0 || isdefault || strncmp(jobid, (const char*) jobHostId, jobHostId.length()) == 0) {
	    // This appears to be a job on this host.
	    Job* job = preJobCmd("delete", jobid + (isdefault ? 0 : jobHostId.length()), emsg);
	    if (job) {
		const char* startdir = cwd->pathname;
		if (Sys::chdir("/") < 0) {
		    reply(504, "Cannot change to base spool directory.");
		    return;
		}
		if (job->state != FaxRequest::state_done &&
		  job->state != FaxRequest::state_failed &&
		  job->state != FaxRequest::state_suspended) {
		    reply(504, "Job %s not deleted; use JSUSP first.", jobid);
		    return;
		}
		if (!lockJob(*job, LOCK_EX, emsg)) {
		    reply(504, "Cannot delete job: %s.", (const char*) emsg);
		    return;
		}
		/*
		 * Jobs in the doneq (state_done) have had their
		 * documents converted to references (w/o links)
		 * to the base document name; thus there is no
		 * work to do to cleanup document state (a separate
		 * scavenger program must deal with this since it
		 * requires global knowledge of what jobs reference
		 * what documents).
		 *
		 * Jobs that have yet to complete however hold links
		 * to documents that must be removed.  We do this here
		 * and also notify the scheduler about our work so that
		 * it can properly expunge imaged versions of the docs.
		 */
		if (job->state == FaxRequest::state_suspended) {
		    for (u_int i = 0, n = job->items.length(); i < n; i++) {
			const FaxItem& fitem = job->items[i];
			switch (fitem.op) {
			case FaxRequest::send_fax:
			    if (sendQueuerACK(emsg, "U%s", (const char*) fitem.item) ||
			      !job->isUnreferenced(i))
				break;
			    /* ... fall thru */
			case FaxRequest::send_tiff_saved:
			case FaxRequest::send_tiff:
			case FaxRequest::send_pdf_saved:
			case FaxRequest::send_pdf:
			case FaxRequest::send_postscript:
			case FaxRequest::send_postscript_saved:
			case FaxRequest::send_pcl:
			case FaxRequest::send_pcl_saved:
			case FaxRequest::send_data:
			    Sys::unlink(fitem.item);
			    break;
			}
		    }
		} else {
		    // expunge any cover page documents
		    for (u_int i = 0, n = job->items.length(); i < n; i++) {
			const FaxItem& fitem = job->items[i];
			switch (fitem.op) {
			case FaxRequest::send_tiff_saved:
			case FaxRequest::send_tiff:
			case FaxRequest::send_pdf_saved:
			case FaxRequest::send_pdf:
			case FaxRequest::send_postscript:
			case FaxRequest::send_postscript_saved:
			case FaxRequest::send_pcl:
			case FaxRequest::send_pcl_saved:
			    if (fitem.item.findR(fitem.item.length(), ".cover"))
				Sys::unlink(fitem.item);
			    break;
			}
		    }
		}
		if (Sys::unlink(job->qfile) < 0)
		    reply(504, "Deletion of queue file %s failed.", (const char*) job->qfile);
		if (Sys::chdir(startdir) < 0)
		    reply(504, "Cannot change to %s spool directory.", startdir);
		jobs.remove(job->jobid);
		if (job == curJob) {			// make default job current
		    curJob = &defJob;
		    curJobId = "default";
		    curJobGroupId = "";
		}
		delete job;				// NB: implicit unlock
		replyCurrentJob(fxStr::format("Job %s deleted; current job:", jobid));
	    }
	    return;
	} else {
	    if (setupJobHost(jobid, emsg)) {
		jobHostClient->jobDelete(jobid);
		fxStr r = jobHostClient->getLastResponse();

		// Extract curJobId and curJobGroupId from response.  Format is:
		// "200 Job XXXX deleted; current job: jobid: XXXX groupid: XXXX."
		// "200 Job XXXX deleted; current job: (default)."
		if (r.find(0, "current job:") < r.length()) {
		    if (r.find(0, "current job: (default)") < r.length()) {
			curJob = &defJob;
			curJobId = "default";
			curJobGroupId = "";
		    } else {
			u_int jp = r.find(0, " jobid: ");
			u_int gp = r.find(jp < r.length()-8 ? jp+8 : 0, " groupid: ");
			if (gp-jp > 8 && r.length()-gp > 11) {
			    curJobId = r.extract(jp+8, gp-jp-8);
			    curJobGroupId = r.extract(gp+10, r.length()-gp-11);
			}
		    }
		}
		reply(-1, (const char*) r);
		return;
	    }
	}
    } else {
	emsg = "missing host id";
    }
    reply(504, "Cannot delete job %s; %s.", jobid, (const char*) emsg);
}

/*
 * Common work for doing job state manipulations.
 */
void
HylaFAXServer::operateOnJob(const char* jobid, const char* what, const char* op)
{
    fxStr emsg;
    if (strlen(jobid) > jobHostId.length()) {
	bool isdefault = (strncmp(jobid, "default", 7) == 0);
	if (jobHostId.length() == 0 || isdefault || strncmp(jobid, (const char*) jobHostId, jobHostId.length()) == 0) {
	    // This appears to be a job on this host.
	    Job* job = preJobCmd(what, jobid + (isdefault ? 0 : jobHostId.length()), emsg);
	    if (job) {
		if (job->state == FaxRequest::state_done ||
		  job->state == FaxRequest::state_failed) {
		    reply(504, "Job %s not %sed; already done.", jobid, what);
		    return;
		}
		if (sendQueuerACK(emsg, "%s%s", op, jobid + jobHostId.length()))
		    reply(200, "Job %s %sed.", jobid, what);
		else
		    reply(460, "Failed to %s job %s: %s.",
			what, jobid, (const char*) emsg);
	    }
	    return;
	} else {
	    if (setupJobHost(jobid, emsg)) {
		if (strncmp(what, "kill", 4) == 0) jobHostClient->jobKill(jobid);
		else if (strncmp(what, "suspend", 7) == 0) jobHostClient->jobSuspend(jobid);
		else if (strncmp(what, "interrupt", 9) == 0) jobHostClient->command((const char*) fxStr::format("JINTR %s", jobid));
		else {
		    reply(460, "Cannot %s job %s; unknown operation.", what, jobid);
		    return;
		}
		reply(-1, (const char*) jobHostClient->getLastResponse());
		return;
	    }
	}
    } else {
	emsg = "missing host id";
    }
    reply(460, "Cannot %s job %s; %s.", what, jobid, (const char*) emsg);
}

/*
 * Terminate a job, potentially aborting any call in progress.
 */
void
HylaFAXServer::killJob(const char* jobid)
{
    operateOnJob(jobid, "kill", "K");
}

/*
 * Suspend a job from being scheduled.
 */
void
HylaFAXServer::suspendJob(const char* jobid)
{
    operateOnJob(jobid, "suspend", "X");
}

/*
 * Interrupt a job from being scheduled.
 */
void
HylaFAXServer::interruptJob(const char* jobid)
{
    operateOnJob(jobid, "interrupt", "Y");
}

void
HylaFAXServer::replyBadJob(const Job& job, Token t)
{
    reply(504, "Cannot submit job %s%s; null or missing %s parameter.",
	(const char*) jobHostId, (const char*) job.jobid, parmToken(t));
}

/*
 * Submit a job for scheduling.
 */
void
HylaFAXServer::submitJob(const char* jobid)
{
    fxStr emsg;
    if (strlen(jobid) > jobHostId.length()) {
	bool isdefault = (strncmp(jobid, "default", 7) == 0);
	if (jobHostId.length() == 0 || isdefault || strncmp(jobid, (const char*) jobHostId, jobHostId.length()) == 0) {
	    // This appears to be a job on this host.
	    Job* job = preJobCmd("submit", jobid + (isdefault ? 0 : jobHostId.length()), emsg);
	    if (job) {
		if (job->state == FaxRequest::state_done ||
		  job->state == FaxRequest::state_failed) {
		    reply(504, "Job %s not submitted; already done.", jobid);
		    return;
		}
		if (job->state != FaxRequest::state_suspended) {
		    reply(504, "Job %s not submitted; use JSUSP first.", jobid);
		    return;
		}
		if (job->number == "")
		    replyBadJob(*job, T_DIALSTRING);
		else if (job->mailaddr == "")
		    replyBadJob(*job, T_NOTIFYADDR);
		else if (job->sender == "")
		    replyBadJob(*job, T_FROM_USER);
		else if (job->modem == "")
		    replyBadJob(*job, T_MODEM);
		else if (job->client == "")
		    replyBadJob(*job, T_CLIENT);
		else {
		    /*
		     * If the client doesn't specify external then use number.  So
		     * temporarily alter job->external as the job is updated on disk.
		     */
		    bool defaultexternal = false;
		    if (job->external == "") {
			job->external = job->number;
			defaultexternal = true;
		    }
		    if (updateJobOnDisk(*job, emsg)) {
			/*
			 * NB: we don't mark the lastmod time for the
			 * job since the scheduler should re-write the
			 * queue file to reflect what it did with it
			 * (e.g. what state it placed the job in).
			 */
			if (sendQueuerACK(emsg, "S%s", jobid + jobHostId.length())) {
			    reply(200, "Job %s submitted.", jobid);
			    Job** jpp = (Job**) blankJobs.find(job->jobid);
			    if (jpp)
				blankJobs.remove(job->jobid);	// it's no longer blank
			} else
			    reply(460, "Failed to submit job %s: %s.",
				jobid, (const char*) emsg);
		    } else
			reply(450, "%s.", (const char*) emsg);	// XXX 550?
		    if (defaultexternal)
			job->external = "";
		}
	    }
	    return;
	} else {
	    if (setupJobHost(jobid, emsg)) {
		jobHostClient->jobSubmit(jobid);
		reply(-1, (const char*) jobHostClient->getLastResponse());
		return;
	    }
	}
    } else {
	emsg = "missing host id";
    }
    reply(460, "Cannot submit job %s; %s.", jobid, (const char*) emsg);
}

/*
 * Wait for a job to complete or for the operation
 * to be aborted.  A data channel is opened and 
 * job status information is returned on it.  The
 * client can terminate this operation with an
 * ABOR command on the control channel; just like
 * a normal file transfer operation.
 */
void
HylaFAXServer::waitForJob(const char* jobid)
{
    fxStr emsg;
    if (strlen(jobid) > jobHostId.length()) {
	bool isdefault = (strncmp(jobid, "default", 7) == 0);
	if (jobHostId.length() == 0 || isdefault || strncmp(jobid, (const char*) jobHostId, jobHostId.length()) == 0) {
	    // This appears to be a job on this host.
	    Job* job = findJob(jobid + (isdefault ? 0 : jobHostId.length()), emsg);
	    if (job) {
		if (job->jobid == "default") {
		    reply(504, "Cannot wait for default job.");
		    return;
		}
		if (job->state == FaxRequest::state_done ||
		  job->state == FaxRequest::state_failed) {
		    reply(216, "Job %s done (already).", jobid);
		    return;
		}
		state &= ~S_LOGTRIG;			// just process events
		if (newTrigger(emsg, "J<%s>%04x", jobid + jobHostId.length(), 1<<Trigger::JOB_DEAD)) {
		    // XXX is lreply the right thing?
		    lreply(216, "Waiting for job %s; use ABOR command to interrupt.",
			jobid);
		    if (setjmp(urgcatch) == 0) {
			Dispatcher& disp = Dispatcher::instance();
			for (state |= S_WAITTRIG; IS(WAITTRIG); disp.dispatch()) {
			    /*
			     * The trigger event handlers update our notion
			     * of the job state asynchronously so we can just
			     * monitor the job's state variable.  Beware however
			     * that the job may get removed/moved to the doneq
			     * while we're monitoring its status; so we cannot
			     * blindly hold a reference to the in-memory structure.
			     */
			    job = findJob(jobid + jobHostId.length(), emsg);
			    if (!job || job->state == FaxRequest::state_done ||
			      job->state == FaxRequest::state_failed)
				break;
			}
			reply(216, "Wait for job %s completed.", jobid);
		    }
		    state &= ~S_WAITTRIG;
		    (void) cancelTrigger(emsg);
		} else
		    reply(504, "Cannot register trigger: %s.", (const char*) emsg);
		return;
	    }
	} else {
	    if (setupJobHost(jobid, emsg)) {
		jobHostClient->jobWait(jobid);
		reply(-1, (const char*) jobHostClient->getLastResponse());
		return;
	    }
	}
    } else {
	emsg = "missing host id";
    }
    reply(500, "Cannot wait for job %s; %s.", jobid, (const char*) emsg);
}

/*
 * Do common work used in adding a document to a
 * job's set of documents that are to be sent.
 */
bool
HylaFAXServer::checkAddDocument(Job& job, Token type,
    const char* docname, FaxSendOp& op)
{
    if (checkParm(job, type, A_WRITE)) {
	struct stat sb;
	if (fileAccess(docname, R_OK, sb)) {
	    fxStr file(docname);
	    u_int d = file.nextR(file.length(), '.');
	    /*
	     * We trust the client's protocol-specified (FORM) file format except 
	     * in the case of Postscript because Postscript is the server-default 
	     * (in the case the client did not specify the format).
	     */
	    if (strcmp(docname+d, "ps") != 0) {
		for (u_int i = 0, n = N(formats); i < n; i++) {
		    if (strcmp(docname+d, formats[i].suffix) == 0 && formats[i].supported) {
			op = formats[i].op;
			return (true);
		    }
		}
	    }
	    if (!docType(docname, op))
		reply(550, "%s: Document type not recognized.", docname);
	    else
		return (true);
	}
    }
    return (false);
}

/*
 * Add a cover document to the current job's
 * set of documents that are to be sent.
 */
void
HylaFAXServer::addCoverDocument(Job& job, const char* docname)
{
    FaxSendOp op;
    if (checkAddDocument(job, T_COVER, docname, op)) {
	fxStr covername = "/" FAX_DOCDIR "/cover" | job.jobid | ".cover";
	if (Sys::link(docname, covername) >= 0) {
	    // XXX mark as cover page
	    job.items.append(FaxItem(op, 0, "", &covername[1]));
	    reply(200, "Added cover page document %s as %s.",
		docname, &covername[1]);
	    job.pagehandling = "";		// force recalculation
	} else
	    reply(550, "Unable to link cover page document %s to %s: %s.",
		docname, (const char*) covername, strerror(errno));
    }
}

/*
 * Add a non-cover document to the current
 * job's set of documents that are to be sent.
 */
void
HylaFAXServer::addDocument(Job& job, const char* docname)
{
    FaxSendOp op;
    if (checkAddDocument(job, T_DOCUMENT, docname, op)) {
	const char* cp = strrchr(docname, '/');
	if (!cp)				// relative name, e.g. doc123
	    cp = docname;
	fxStr document = fxStr::format("/" FAX_DOCDIR "%s.", cp) | job.jobid;
	if (Sys::link(docname, document) >= 0) {
	    job.items.append(FaxItem(op, 0, "", &document[1]));
	    reply(200, "Added document %s as %s.", docname, &document[1]);
	    job.pagehandling = "";		// force recalculation
	} else
	    reply(550, "Unable to link document %s to %s: %s.",
		docname, (const char*) document, strerror(errno));
    }
}

/*
 * Add a polling operation to the current job.
 */
void
HylaFAXServer::addPollOp(Job& job, const char* sep, const char* pwd)
{
    if (checkParm(job, T_POLL, A_WRITE)) {
	job.items.append(FaxItem(FaxRequest::send_poll, 0, sep, pwd));
	reply(200, "Added poll operation.");
    }
}

/*
 * Directory interface support for querying job status.
 */

bool
HylaFAXServer::isVisibleSendQFile(const char* filename, const struct stat&)
{
    if (filename[0] == 'q') {
    	fxStr emsg;
    	Job* job = findJob(&filename[1], emsg);
	if (job && checkAccess(*job, T_JOB, A_READ))
	    return true;
    }
    if (strncmp(filename, FAX_SEQF, 4) == 0) return true;
    return false;
}

#ifdef roundup
#undef roundup
#endif
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

/*
 * Return a compact notation for the specified
 * time.  This notation is guaranteed to fit in
 * a 7-character field.  We select one of 5
 * representations based on how close the time
 * is to ``now''.
 */
const char*
HylaFAXServer::compactTime(time_t t)
{
    time_t now = Sys::now();
    if (t > now) {			// already past
	static char buf[15];
	const struct tm* tm = cvtTime(t);
	if (t < roundup(now, 24*60*60))		// today, use 19:37
	    strftime(buf, sizeof (buf), "%H:%M", tm);
	else if (t < now+7*24*60*60)		// within a week, use Sun 6pm
	    strftime(buf, sizeof (buf), "%a%I%p", tm);
	else					// over a week, use 25Dec95
	    strftime(buf, sizeof (buf), "%d%b%y", tm);
	return (buf);
    } else
	return ("");
}

static const char jformat[] = {
    's',		// A (subaddr)
    's',		// B (passwd)
    's',		// C (company)
    's',		// D (totdials & maxdials)
    'u',		// E (desiredbr)
    's',		// F (tagline)
    'u',		// G (desiredst)
    'u',		// H (desireddf)
    'u',		// I (usrpri)
    's',		// J (jobtag)
    'c',		// K (desiredec as symbol)
    's',		// L (location)
    's',		// M (mailaddr)
    'c',		// N (desiredtl as symbol)
    'c',		// O (useccover as symbol)
    's',		// P (npages & total pages)
    'u',		// Q (minbr)
    's',		// R (receiver)
    's',		// S (sender)
    's',		// T (tottries & maxtries)
    's',		// U (chopthreshold)
    's',		// V (doneop)
    's',		// W (commid)
    'c',		// X (jobtype as symbol)
    's',		// Y (tts in strftime %Y/%m/%d %H.%M.%S format)
    'u',		// Z (tts as decimal time_t)
    '[',		// [
    '\\',		// \ (must have something after the backslash)
    ']',		// ]
    '^',		// ^
    '_',		// _
    '`',		// `
    'c',		// a (state as symbol)
    'u',		// b (ntries)
    's',		// c (client)
    'u',		// d (totdials)
    's',		// e (external)
    'u',		// f (ndials)
    's',		// g (groupid)
    'c',		// h (pagechop as symbol)
    'u',		// i (pri)
    's',		// j (jobid)
    's',		// k (killtime)
    'u',		// l (pagelength)
    's',		// m (modem)
    'c',		// n (notify as symbol)
    's',		// o (owner)
    'u',		// p (npages)
    's',		// q (retrytime)
    'u',		// r (resolution)
    's',		// s (notice a.k.a. status)
    'u',		// t (tottries)
    'u',		// u (maxtries)
    's',		// v (number a.k.a dialstring)
    'u',		// w (pagewidth)
    'u',		// x (maxdials)
    'u',		// y (total pages)
    's',		// z (tts)
    'c',		// 0 (usexvres as symbol)
    'c',		// 1 (usecolor as symbol)
};

/*
 * Print a formatted string with fields filled in from
 * the specified job's state.  This functionality is
 * used to permit clients to get job state listings in
 * preferred formats.
 */
void
HylaFAXServer::Jprintf(FILE* fd, const char* fmt, const Job& job)
{
    /*
     * Check once to see if the client has access to
     * privileged job state.  This typically is not
     * needed but doing it here eliminates the need to
     * do more work below (and the check should be fast).
     *
     * NB: This assumes that read access to T_DIALSTRING 
     *     implies read access to anything else in the
     *     job state that is protected.
     */
    bool haveAccess = checkAccess(job, T_DIALSTRING, A_READ);
    for (const char* cp = fmt; *cp; cp++) {
	if (*cp == '%') {
#define	MAXSPEC	20
	    char fspec[MAXSPEC];
	    char* fp = fspec;
	    *fp++ = '%';
	    char c = *++cp;
	    if (c == '-')
		*fp++ = c, c = *++cp;
	    if (isdigit(c)) {
		do {
		    *fp++ = c;
		} while (isdigit(c = *++cp) && fp < &fspec[MAXSPEC-3]);
	    }
	    if (c == '.') {
		do {
		    *fp++ = c;
		} while (isdigit(c = *++cp) && fp < &fspec[MAXSPEC-2]);
	    }
	    if (!isalpha(c)) {
		if (c == '%')		// %% -> %
		    putc(c, fd);
		else
		    fprintf(fd, "%.*s%c", (int) (fp-fspec), fspec, c);
		continue;
	    }
	    fp[0] = jformat[c-'A'];	// printf format string
	    fp[1] = '\0';
            switch (c) {
	    case 'A':
		fprintf(fd, fspec, (const char*) job.subaddr);
		break;
	    case 'B':
		fprintf(fd, fspec, haveAccess ? (const char*) job.passwd : "");
		break;
	    case 'C':
		fprintf(fd, fspec, (const char*) job.company);
		break;
	    case 'D':
		fprintf(fd, fspec, (const char*)fxStr::format("%2u:%-2u", job.totdials, job.maxdials));
		break;
	    case 'E':
		fprintf(fd, fspec, job.desiredbr);
		break;
	    case 'F':
		fprintf(fd, fspec, (const char*) job.tagline);
		break;
	    case 'G':
		fprintf(fd, fspec, job.desiredst);
		break;
	    case 'H':
		fprintf(fd, fspec, job.desireddf);
		break;
	    case 'I':
		fprintf(fd, fspec, job.usrpri);
		break;
	    case 'J':
		fprintf(fd, fspec, (const char*) job.jobtag);
		break;
	    case 'K':
		fprintf(fd, fspec, "D HF"[job.desiredec]);
		break;
	    case 'L':
		fprintf(fd, fspec, (const char*) job.location);
		break;
	    case 'M':
		fprintf(fd, fspec, (const char*) job.mailaddr);
		break;
	    case 'N':
		fprintf(fd, fspec, " P"[job.desiredtl]);
		break;
	    case 'O':
		fprintf(fd, fspec, "N "[job.useccover]);
		break;
	    case 'P':
		fprintf(fd, fspec, (const char*)fxStr::format("%2u:%-2u", job.npages, job.totpages));
		break;
	    case 'Q':
		fprintf(fd, fspec, job.minbr);
		break;
	    case 'R':
		fprintf(fd, fspec, (const char*) job.receiver);
		break;
	    case 'S':
		fprintf(fd, fspec, (const char*) job.sender);
		break;
	    case 'T':
		fprintf(fd, fspec, (const char*)fxStr::format("%2u:%-2u", job.tottries, job.maxtries));
		break;
	    case 'U':
		fprintf(fd, fspec, (const char*)fxStr::format("%.1f", job.chopthreshold));
		break;
	    case 'V':
		fprintf(fd, fspec, (const char*) job.doneop);
		break;
	    case 'W':
		fprintf(fd, fspec, (const char*) job.commid);
		break;
	    case 'X':
		fprintf(fd, fspec, toupper(job.jobtype[0]));
		break;
	    case 'Y':
		{ char buf[30];				// XXX HP C++
		  strftime(buf, sizeof (buf), "%Y/%m/%d %H.%M.%S",
			IS(USEGMT) ? gmtime(&job.tts) : localtime(&job.tts));
		  fprintf(fd, fspec, buf);
		}
		break;
	    case 'Z':
		fprintf(fd, fspec, job.tts);
		break;
	    case 'a':
		fprintf(fd, fspec, "?TPSBWRDF"[job.state]);
		break;
	    case 'b':
		fprintf(fd, fspec, job.ntries);
		break;
	    case 'c':
		fprintf(fd, fspec, (const char*) job.client);
		break;
	    case 'd':
		fprintf(fd, fspec, job.totdials);
		break;
	    case 'e':
		fprintf(fd, fspec, (const char*) job.external);
		break;
	    case 'f':
		fprintf(fd, fspec, job.ndials);
		break;
	    case 'g':
		fprintf(fd, fspec, (const char*) job.groupid);
		break;
	    case 'h':
		fprintf(fd, fspec, " DAL"[job.pagechop]);
		break;
	    case 'i':
		fprintf(fd, fspec, job.pri);
		break;
	    case 'j':
		fprintf(fd, fspec, (const char*) (jobHostId | job.jobid));
		break;
	    case 'k':
		fprintf(fd, fspec, compactTime(job.killtime));
		break;
	    case 'l':
		fprintf(fd, fspec, job.pagelength);
		break;
	    case 'm':
		fprintf(fd, fspec, (const char*) job.modem);
		break;
	    case 'n':
		fprintf(fd, fspec, " DQA"[job.notify]);
		break;
	    case 'o':
		fprintf(fd, fspec, (const char*) job.owner);
		break;
	    case 'p':
		fprintf(fd, fspec, job.npages);
		break;
	    case 'q':
		fprintf(fd, fspec,
		    job.retrytime == 0 ? "" : fmtTime(job.retrytime));
		break;
	    case 'r':
		fprintf(fd, fspec, job.resolution);
		break;
	    case 's':
		fprintf(fd, fspec, (const char*) job.notice);
		break;
	    case 't':
		fprintf(fd, fspec, job.tottries);
		break;
	    case 'u':
		fprintf(fd, fspec, job.maxtries);
		break;
	    case 'v':
		fprintf(fd, fspec, haveAccess ? (const char*) job.number : "");
		break;
	    case 'w':
		fprintf(fd, fspec, job.pagewidth);
		break;
	    case 'x':
		fprintf(fd, fspec, job.maxdials);
		break;
	    case 'y':
		fprintf(fd, fspec, job.totpages);
		break;
	    case 'z':
		fprintf(fd, fspec, compactTime(job.tts));
		break;
	    case '0':
		fprintf(fd, fspec, "N "[job.usexvres]);
		break;
	    case '1':
		fprintf(fd, fspec, "N "[job.usecolor]);
		break;
	    }
	} else
	    putc(*cp, fd);
    }
}

void
HylaFAXServer::listSendQ(FILE* fd, const SpoolDir&, DIR* dir)
{
    fxStrArray files;
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	files.append(dp->d_name);
    }
    files.qsort();
    for (u_int i = 0, n = files.length(); i < n; i++) {
	if (files[i][0] == 'q') {
	    fxStr emsg;
	    Job* job = findJob(&((const char*)files[i])[1], emsg);
	    if (job) {
		Jprintf(fd, jobFormat, *job);
		fputs("\r\n", fd);
	    }
	}
    }
}

void
HylaFAXServer::listSendQFile(FILE* fd, const SpoolDir& dir,
    const char* filename, const struct stat& sb)
{
    fxStr emsg;
    Job* job = findJob(filename, emsg);
    if (job)
	Jprintf(fd, jobFormat, *job);
    else
	listUnixFile(fd, dir, filename, sb);
}

void
HylaFAXServer::nlstSendQ(FILE* fd, const SpoolDir&, DIR* dir)
{
    fxStrArray files;
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	files.append(dp->d_name);
    }
    files.qsort();
    for (u_int i = 0, n = files.length(); i < n; i++) {
	if (files[i][0] == 'q') {
	    fxStr emsg;
	    Job* job = findJob(&((const char*)files[i])[1], emsg);
	    if (job)
		Jprintf(fd, "%j\r\n", *job);
	}
    }
}

void
HylaFAXServer::nlstSendQFile(FILE* fd, const SpoolDir&,
    const char* filename, const struct stat&)
{
    fxStr emsg;
    Job* job = findJob(filename, emsg);
    if (job)
	Jprintf(fd, "%j", *job);
    else
	fprintf(fd, "%s", filename);
}
