/*	$Id: faxrm.c++ 945 2009-09-29 11:46:02Z faxguy $ */
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
#include "FaxClient.h"
#include "Sys.h"
#include "config.h"

class faxRmApp : public FaxClient {
private:
    void usage();

    bool removeJob(const char* id, fxStr& emsg);
    bool deleteDoc(const char* id);
public:
    faxRmApp();
    ~faxRmApp();

    bool run(int argc, char** argv);
};

faxRmApp::faxRmApp() {}
faxRmApp::~faxRmApp() {}

bool
faxRmApp::run(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int c;

    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_USERCONF);

    bool jobs = true;
    bool docs = false;
    bool useadmin = false;
    char* pass = NULL;
    char* adminpass = NULL;
    char* user = NULL;
    int errcnt = 0; // count how many jobs couldn't be removed

    while ((c = Sys::getopt(argc, argv, "ah:dO:p:u:v")) != -1)
	switch (c) {
	case 'a':
	    useadmin = true;
	    break;
	case 'd':			// treat args as document names
	    jobs = false;
	    docs = true;
	    break;
	case 'h':			// server's host
	    setHost(optarg);
	    break;
        case 'p':			// password:adminpass
	    {
		char* pp = strchr(optarg, ':');
		if (pp && *(pp + 1) != '\0') {
		    *pp = '\0';
		    adminpass = pp + 1;
		}
	    }
	    pass = optarg;
	    break;
        case 'u':			// user
	    user = optarg;
	    break;
	case 'O':
	    readConfigItem(optarg);
	    break;
	case 'v':
	    setVerbose(true);
	    break;
	case '?':
	    usage();
	}
    if (optind >= argc)
	usage();
    fxStr emsg;
    if (callServer(emsg)) {
	if (login(user, pass, emsg) &&
	    (!useadmin || admin (adminpass, emsg))) {

	    for (; optind < argc; optind++) {
		const char* id = argv[optind];
		if (jobs) {
		    if (!removeJob(id, emsg)) {
			errcnt++;
			break;
                    }
		} else if (docs) {
		    if (!deleteDoc(id)) {
			emsg = getLastResponse();
                        errcnt++;
			break;
		    }
		    printf("%s removed.\n", id);
		}
	    }
	}
	hangupServer();
    }
    if (emsg != "")
	printError("%s", (const char*) emsg);
    if (errcnt != 0)
        return (false);
    return(true);
}

bool
faxRmApp::removeJob(const char* id, fxStr& emsg)
{
    if (jobKill(id)) {
	printf("Job %s removed.\n", id);
	return (true);
    }
    emsg = getLastResponse();
    if (getLastCode() == 504 && jobDelete(id)) {
	printf("Job %s removed (from doneq).\n", id);
	emsg = "";
	return (true);
    }
    return (false);
}

bool
faxRmApp::deleteDoc(const char* id)
{
    return (command("DELE %s%s"
	, id[0] == '/' || strncmp(id, FAX_DOCDIR, sizeof (FAX_DOCDIR)-1) == 0 ?
	    "" : "/" FAX_DOCDIR "/"
	, id
	) == COMPLETE);
}

void
faxRmApp::usage()
{
    fxFatal("usage: faxrm [-h server-host] [-u user] [-p pass[:admin]] [-adv] id...");
}

int
main(int argc, char** argv)
{
    faxRmApp app;
    if(!app.run(argc, argv))
        return 1;
    return 0;
}
