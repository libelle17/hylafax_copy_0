/*	$Id: FaxAcctInfo.c++ 1146 2013-02-15 22:29:39Z faxguy $ */
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
#include "FaxAcctInfo.h"
#include "StackBuffer.h"
#include "Sys.h"
#include <sys/file.h>

#include "config.h"

extern	const char* fmtTime(time_t);

/*
 * Record an activity in the transfer log file.
 */
bool
FaxAcctInfo::record(const char* cmd)
{
    bool ok = false;
    int fd = Sys::open(FAX_XFERLOG, O_RDWR|O_CREAT|O_APPEND, 0644);

    char* timebuf = (char*) malloc(80);
    strftime(timebuf, 79, "%D %H:%M", localtime(&start));

    char* jobtagbuf = (char*) malloc(80);
    u_int i = 0;
    char c;
    for (const char* cp = jobtag; (c = *cp); cp++) {
	if (i == 79)				// truncate string
	    break;
	if (c == '\t')				// tabs are field delimiters
	    c = ' ';
	else if (c == '"')			// escape quote marks
	    jobtagbuf[i++] = '\\';
	jobtagbuf[i++] = c;
    }
    jobtagbuf[i] = '\0';

    fxStr paramsbuf = fxStr::format("%u", params);
    fxStr npagesbuf = fxStr::format("%d", npages);
    fxStr durationbuf = fxStr::format("%s", fmtTime(duration));
    fxStr conntimebuf = fxStr::format("%s", fmtTime(conntime));

    fxStr callid_formatted = "";
    for (i = 2; i < callid.size(); i++) {
	if (i > 2) callid_formatted.append("::");
	callid_formatted.append(callid[i]);
    }

    if (fd >= 0) {
	fxStackBuffer record;
	record.put(timebuf);			// $ 1 = time
	record.fput("\t%s", cmd);		// $ 2 = SEND|RECV|POLL|PAGE
	record.fput("\t%s", commid);		// $ 3 = commid
	record.fput("\t%s", device);		// $ 4 = device
	record.fput("\t%s", jobid);		// $ 5 = jobid
	record.fput("\t\"%s\"", jobtagbuf);	// $ 6 = jobtag
	record.fput("\t%s", user);		// $ 7 = sender
	record.fput("\t\"%s\"", dest);		// $ 8 = dest
	record.fput("\t\"%s\"", csi);		// $ 9 = csi
	record.fput("\t%u", params);		// $10 = encoded params and DCS
	record.fput("\t%d", npages);		// $11 = npages
	record.fput("\t%s", fmtTime(duration));	// $12 = duration
	record.fput("\t%s", fmtTime(conntime));	// $13 = conntime
	record.fput("\t\"%s\"", status);	// $14 = status
	record.fput("\t\"%s\"", callid.size() > CallID::NAME ? (const char*) callid[1] : "");	// $15 = CallID2/CIDName
	record.fput("\t\"%s\"", callid.size() > CallID::NUMBER ? (const char*) callid[0] : "");	// $16 = CallID1/CIDNumber
	record.fput("\t\"%s\"", (const char*) callid_formatted);	// $17 = CallID3 -> CallIDn
	record.fput("\t\"%s\"", owner);					// $18 = owner
	record.fput("\t\"%s\"", (const char*) faxdcs);			// $19 = DCS
	record.fput("\t%s", (const char*) jobinfo);			// $20 = jobinfo
	record.put('\n');
	flock(fd, LOCK_EX);
	ok = (Sys::write(fd, record, record.getLength()) == (ssize_t)record.getLength());
	Sys::close(fd);				// implicit unlock
    }

    /*
     * Here we provide a hook for an external accounting
     * facility, such as a database.
     */
    const char* argv[22];
    argv[0] = "FaxAccounting";
    argv[1] = timebuf;
    argv[2] = cmd;
    argv[3] = commid;
    argv[4] = device;
    argv[5] = jobid;
    argv[6] = jobtagbuf;
    argv[7] = user;
    argv[8] = dest;
    argv[9] = csi;
    argv[10] = (const char*) paramsbuf;
    argv[11] = (const char*) npagesbuf;
    argv[12] = (const char*) durationbuf;
    argv[13] = (const char*) conntimebuf;
    argv[14] = status;
    argv[15] = callid.size() > CallID::NAME ? (const char*) callid[1] : "";
    argv[16] = callid.size() > CallID::NUMBER ? (const char*) callid[0] : "";
    argv[17] = (const char*) callid_formatted;
    argv[18] = owner;
    argv[19] = (const char*) faxdcs;
    argv[20] = (const char*) jobinfo;
    argv[21] = NULL;
    pid_t pid = fork();		// signal handling in some apps seems to require a fork here
    switch (pid) {
	case 0:
	{
	    int fd = Sys::open(_PATH_DEVNULL, O_RDWR);
	    dup2(fd, STDIN_FILENO);
	    dup2(fd, STDOUT_FILENO);
	    dup2(fd, STDERR_FILENO);
	    for (int f = Sys::getOpenMax()-1; f >= 0; f--)
		if (f != STDIN_FILENO && f != STDOUT_FILENO && f != STDERR_FILENO) Sys::close(f);
	    setsid();
	    Sys::execv("etc/FaxAccounting", (char* const*) argv);
	    sleep(1);		// XXX give parent time
	    _exit(127);
	}
	case -1:
	    break;
	default:
	    // The child process disassociates itself, so we don't need to: Sys::waitpid(pid);
	    break;
    }
    return (ok);
}
