/*	$Id: Login.c++ 1186 2013-07-31 17:05:45Z faxguy $ */
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
#include "HylaFAXServer.h"
#include "Sys.h"

#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#if HAS_CRYPT_H
#include <crypt.h>
#endif

void
HylaFAXServer::loginRefused(const char* why)
{
    if (++loginAttempts >= maxLoginAttempts) {
	reply(530, "Login incorrect (closing connection).");
	logNotice("Repeated login failures for user %s from %s [%s]"
	    , (const char*) the_user
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	);
	dologout(0);
    } else {
	reply(530, "User %s access denied.", (const char*) the_user);
	logNotice("HylaFAX LOGIN REFUSED (%s) FROM %s [%s], %s"
	    , why
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
    }
}

/*
 * USER command. Sets global passwd state if named
 * account exists and is acceptable; sets askpasswd if a
 * PASS command is expected.  If logged in previously,
 * need to reset state.  User account must be accessible
 * from client host according to the contents of the
 * userAccessFile.
 */
void
HylaFAXServer::userCmd(const char* name)
{
    if (IS(LOGGEDIN)) {
	if (IS(PRIVILEGED) && the_user == name) {// revert to unprivileged mode
	    state &= ~S_PRIVILEGED;
	    reply(230, "User %s logged in.", name);
	    return;
	}
        end_login();
    }
    the_user = name;
    state &= ~S_PRIVILEGED;
    adminWd = "*";			// make sure no admin privileges
    passWd = "*";			// just in case...
    usrAdminWd = "*";
    usrPassWd = "*";

    if (curJobHost != -1) {
 	// There is a connection to a job host.  Disconnect it.
 	jobHostClient->hangupServer();
 	curJobHost = -1;
    }

    if (checkUser(name)) {
	if (passWd != "") {
	    state |= S_WAITPASS;
	    reply(331, "Password required for %s.", name);
	    /*
	     * Delay before reading passWd after first failed
	     * attempt to slow down password-guessing programs.
	     */
	    if (loginAttempts)
		sleep(loginAttempts);
	} else
	    login(230);
    } else
	loginRefused("user denied");
}

#ifdef HAVE_PAM
int
pamconv(int num_msg, STRUCT_PAM_MESSAGE **msg, struct pam_response **resp, void *appdata)
{
	/*
	 * This PAM conversation function expects that the PAM modules
	 * being used will only have one message.  If this expectation
	 * is not met then this will fail, and this will need modification
	 * in order to work.
	 */

	char *password =(char*) appdata;
	struct pam_response* replies;

	if (num_msg != 1 || msg[0]->msg_style != PAM_PROMPT_ECHO_OFF)
	    return PAM_CONV_ERR;

	if (password == NULL) {
	    /*
	     * Solaris doesn't have PAM_CONV_AGAIN defined.
	     */
	    #ifdef PAM_CONV_AGAIN
		return PAM_CONV_AGAIN;
	    #else
		return PAM_CONV_ERR;
	    #endif
	}

	replies=(struct pam_response*)calloc(num_msg, sizeof(struct pam_response));

	replies[0].resp = password;
	replies[0].resp_retcode = 0;
	*resp = replies;

	return (PAM_SUCCESS);
}
#endif //HAVE_PAM

bool
HylaFAXServer::pamIsAdmin(const char* user)
{
	bool retval = false;
#ifdef HAVE_PAM
	int i;
	static struct group* grinfo = getgrnam(admingroup);
	const char *curruser = (user == NULL ? the_user.c_str() : user);
	if (grinfo != NULL) {
		for (i=0; grinfo->gr_mem[i] != NULL; i++) {
			if (strcmp(curruser, grinfo->gr_mem[i]) == 0) retval = true;
		}
	}
#endif //HAVE_PAM
	return(retval);
}

/**
* ldapCheck
* function checks if user with selected login and password exists in LDAP
* param user [IN] - pointer to string containing user login
* param pass [IN] - pointer to string containing user password
* return true if user exists
*/
bool
HylaFAXServer::ldapCheck(const char* user, const char* pass)
{
	bool retval = false;
#ifdef HAVE_LDAP
	int err = 0, i = 0;
	LDAPMessage* pEntries = NULL;
	LDAPMessage* pEntry = NULL;
	struct berval **p_arr_values = NULL;
	struct berval s_UserPasswd = {0};
	LDAP* p_LDAPConn = NULL;
	bool bValidUser = false;
	char filter[255] = "";
	char sLDAPUserDN[255] = "";

	/*
	 * See if ldapServerUri has a value.
	 * If not, disable using LDAP support.
	 */
	if (strlen(ldapServerUri) == 0) {
		goto cleanup;
	}

// FIXME: Should we leak these error message to an unathenticated client?
	if (ldapBaseDN.length() == 0){
		reply(530, "LDAP misconfigured (ldapBaseDN)");
		goto cleanup;
	}

	if (ldapReqGroup.length() == 0){
		reply(530, "LDAP misconfigured (ldapReqGroup)");
		goto cleanup;
	}

	if (ldapVersion < 2){
		reply(530, "LDAP misconfigured (ldapVersion)");
		goto cleanup;
	}

	// Avoid NULL pointers, empty strings, incomplete LDAP configs.
	if (!user || !strlen(user) || !pass) {
		goto cleanup;
	}

	i = snprintf(filter, sizeof(filter), "uid=%s", user);

	// Did we overflow?  If so, then just fail the authentication.
	if (i >= sizeof(filter)) {
		goto cleanup;
	}

	/*
	 * Build the User DN using the LDAP Base value
	 * from the config file and the supplied username
	 */
	i = snprintf(sLDAPUserDN, sizeof(sLDAPUserDN), "cn=%s,%s", user, (const char*)ldapBaseDN);

	// Did we overflow?  If so, then just fail the authentication.
	if (i >= sizeof(sLDAPUserDN)) {
		goto cleanup;
	}

	/*
	 * Store the password in the berval struct for ldap_sasl_bind_s
	 */
	s_UserPasswd.bv_val = (char *)pass;
	s_UserPasswd.bv_len = strlen(pass);


	/*
	 * Connect to the LDAP server and set the version
	 */
	ldap_initialize(&p_LDAPConn, ldapServerUri);
	if (p_LDAPConn == NULL) {
		reply(530, "Unable to connect to LDAP");
		goto cleanup;
	}

	err = ldap_set_option(p_LDAPConn, LDAP_OPT_PROTOCOL_VERSION, (void *) &ldapVersion);
	if (err != LDAP_SUCCESS) {
		reply(530, "Set Option LDAP error %d: %s", err, ldap_err2string(err));
		goto cleanup;
	}


	/*
	 * Attempt a simple bind to the LDAP server
	 * using the user credentials given to us.
	 *
	 * If we fail, then the provided credentials
	 * are incorrect
	 */
	err = ldap_sasl_bind_s(p_LDAPConn, sLDAPUserDN, LDAP_SASL_SIMPLE, &s_UserPasswd, NULL, NULL, NULL);
	if (err != LDAP_SUCCESS) {
		reply(530, "Bind LDAP error %d: %s", err, ldap_err2string(err));
		goto cleanup;
	}


	/*
	 * Search the LDAP tree for the group that
	 * a user needs to be in to have fax server
	 * access.
	 */
	err = ldap_search_ext_s(p_LDAPConn, sLDAPUserDN, LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, NULL, 0, &pEntries);
	if (err != LDAP_SUCCESS) {
		reply(530, "Search LDAP error %d: %s", err, ldap_err2string(err));
		goto cleanup;
	}

	/*
	 * Get the first entry, which should be
	 * our desired user
	 */
	pEntry = ldap_first_entry(p_LDAPConn, pEntries);
	if (pEntry == NULL) {
		reply(530, "LDAP user not found");
		goto cleanup;
	}

	/*
	 * Fetch all of the groupMembership values
	 */
	p_arr_values = ldap_get_values_len(p_LDAPConn, pEntry, "groupMembership");
	if (p_arr_values == NULL) {
		reply(530, "LDAP attribute groupMembership not found");
		goto cleanup;
	}

	/*
	 * Check each value to see if it matches
	 * our desired value specifed in the config
	 */
	i = 0;
	while (p_arr_values[i] != NULL) {
		if (strcmp(ldapReqGroup, p_arr_values[i]->bv_val) == 0)	{
			bValidUser = true;
			retval = true;
			break;
		}
		i++;
	}

	if (!bValidUser) {
		reply(530, "Access Denied");
	}

cleanup:
	if (p_arr_values) {
		ldap_value_free_len(p_arr_values);
		p_arr_values = NULL;
	}

	if (p_LDAPConn) {
		ldap_unbind_ext_s(p_LDAPConn, NULL, NULL);
		p_LDAPConn = NULL;
	}

#endif // HAVE_LDAP
	return retval;
}

bool
HylaFAXServer::pamCheck(const char* user, const char* pass)
{
	bool retval = false;
#ifdef HAVE_PAM

	if (user == NULL) user = the_user;
	if (pass == NULL) pass = passWd.c_str();

	/*
	 * The effective uid must be privileged enough to
	 * handle whatever the PAM module may require.
	 */
	uid_t ouid = geteuid();
	(void) seteuid(0);

	pam_handle_t *pamh;
	struct pam_conv conv = {pamconv, NULL};
	conv.appdata_ptr = strdup(pass);

	int pamret = pam_start(FAX_SERVICE, user, &conv, &pamh);
	if (pamret == PAM_SUCCESS) {
	    pamret = pam_set_item(pamh, PAM_RHOST, remoteaddr);
	    if (pamret == PAM_SUCCESS) {
		pamret = pam_set_item(pamh, PAM_CONV, (const void*)&conv);
		if (pamret == PAM_SUCCESS) {
#ifdef PAM_INCOMPLETE
		    u_int tries = 10;
		    do {
			/*
			 * PAM supports event-driven applications by returning PAM_INCOMPLETE
			 * and requiring the application to recall pam_authenticate after the
			 * underlying PAM module is ready.  The application is supposed to
			 * utilize the pam_conv structure to determine when the authentication
			 * module is ready.  However, in our case we're not event-driven, and
			 * so we can wait, and a call to sleep saves us the headache.
			 */
#endif // PAM_INCOMPLETE
			pamret = pam_authenticate(pamh, 0);
#ifdef PAM_INCOMPLETE
		    } while (pamret == PAM_INCOMPLETE && --tries && !sleep(1));
#endif // PAM_INCOMPLETE
		    if (pamret == PAM_SUCCESS) {
			pamret = pam_acct_mgmt(pamh, 0);
			if (pamret == PAM_SUCCESS) {
			    retval = true;
			} else
			    logNotice("pam_acct_mgmt failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
		    } else
			logNotice("pam_authenticate failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
		} else
		    logNotice("pam_set_item (PAM_CONV) failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
	    } else
		logNotice("pam_set_item (PAM_RHOST) failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
	} else
	    logNotice("pam_start failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));

	pamEnd(pamh, pamret);
	(void) seteuid(ouid);
#endif
	return retval;
}

#ifdef HAVE_PAM
void HylaFAXServer::pamEnd(pam_handle_t *pamh, int pamret)
{
    if (pamret == PAM_SUCCESS)
    {
	if (pamIsAdmin())
	    state |= S_PRIVILEGED;

	char *newname=NULL;

	/*
	 * Solaris has proprietary pam_[sg]et_item() extension.
	 * Sun defines PAM_MSG_VERSION therefore is possible to use
	 * it in order to recognize the extensions of Solaris
	 */
	#ifdef PAM_MSG_VERSION
	    pamret = pam_get_item(pamh, PAM_USER, (void **)&newname);
	#else
	    pamret = pam_get_item(pamh, PAM_USER, (const void **)&newname);
	#endif

	if (pamret == PAM_SUCCESS && newname != NULL)
	    the_user = strdup(newname);

	struct passwd* uinfo=getpwnam((const char *)the_user);
	if (uinfo != NULL) {
	    uid = uinfo->pw_uid;
	}	

    }
    pamret = pam_end(pamh, pamret);
    pamh = NULL;
}
#endif //HAVE_PAM

void
HylaFAXServer::passCmd(const char* pass)
{
    if (IS(LOGGEDIN)) {
        reply(503, "Already logged in as USER %s.", (const char*) the_user);
        return;
    }
    if (!IS(WAITPASS)) {
        reply(503, "Login with USER first.");
        return;
    }
    state &= ~S_WAITPASS;

    /*
     * Disable long reply messages for old (broken) FTP
     * clients if the first character of the password
     * is a ``-''.
     */
    if (pass[0] == '-') {
	state &= ~S_LREPLIES;
	pass++;
    } else
	state |= S_LREPLIES;

    /*
     * Check hosts.hfaxd first, then PAM, and last, LDAP
     */
    if (pass[0] != '\0') {
	char* ep = crypt(pass, passWd);
	if ((ep && strcmp(ep, passWd) == 0) ||
	    pamCheck(the_user, pass) ||
	    ldapCheck(the_user, pass)) {
	    // log-in was successful
	    usrPassWd = pass;
	    login(230);
	    return;
	}
    }
    if (++loginAttempts >= maxLoginAttempts) {
	reply(530, "Login incorrect (closing connection).");
	logNotice("Repeated login failures for user %s from %s [%s]"
	    , (const char*) the_user
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	);
	dologout(0);
    }
    reply(530, "Login incorrect.");
    logInfo("Login failed from %s [%s], %s"
	, (const char*) remotehost
	, (const char*) remoteaddr
	, (const char*) the_user
    );
    return;
}

/*
 * Login is shared between SNPP and HylaFAX RFC 959 protocol,
 * but different protocols use different codes for success.
 * We make the caller tell us what success is.
 */
void
HylaFAXServer::login(int code)
{
    loginAttempts = 0;		// this time successful
    state |= S_LOGGEDIN;

    uid_t ouid = geteuid();
    (void) seteuid(0);
    bool isSetup = (chroot(".") >= 0 && chdir("/") >= 0);
    /*
     * Install the client's fax-uid as the effective gid
     * so that created files automatically are given the
     * correct ``ownership'' (we store the owner's fax-uid
     * in the group-id field of the inode).
     */
    if (isSetup)
	(void) setegid(uid);
    (void) seteuid(ouid);
    if (!isSetup) {
	reply(550, "Cannot set privileges.");
	end_login();
	return;
    }

#ifdef HAVE_PAM
    pam_chrooted = true;
#endif

    (void) isShutdown(false);	// display any shutdown messages
    reply(code, "User %s logged in.", (const char*) the_user);
    if (TRACE(LOGIN))
	logInfo("FAX LOGIN FROM %s [%s], %s"
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
    (void) umask(077);
    if (tracingLevel & (TRACE_INXFERS|TRACE_OUTXFERS))
        xferfaxlog = Sys::open(xferfaxLogFile, O_WRONLY|O_APPEND|O_CREAT, 0600);

    initDefaultJob();		// setup connection-related state
    dirSetup();			// initialize directory handling
	if (pamIsAdmin()) state |= S_PRIVILEGED;
}

void
HylaFAXServer::adminCmd(const char* pass)
{
    fxAssert(IS(LOGGEDIN), "ADMIN command permitted when not logged in");
    // NB: null adminWd is permitted
    char* ep = crypt(pass, adminWd);
    if ((!ep || strcmp(ep, adminWd) != 0) && !pamIsAdmin()) {
	if (++adminAttempts >= maxAdminAttempts) {
	    reply(530, "Password incorrect (closing connection).");
	    logNotice("Repeated admin failures from %s [%s]"
		, (const char*) remotehost
		, (const char*) remoteaddr
	    );
	    dologout(0);
	} else {
	    reply(530, "Password incorrect.");
	    logInfo("ADMIN failed from %s [%s], %s"
		, (const char*) remotehost
		, (const char*) remoteaddr
		, (const char*) the_user
	    );
	}
	return;
    }
    if (curJobHost != -1) {
	// There is a connection to a job host. Escalate privileges there, too.
	fxStr emsg;
	if (!jobHosts[curJobHost].user.length() && !jobHostClient->admin(pass, emsg)) {
	    emsg = "administrative privileges failed with remote job host";
	    reply(530, (const char*) emsg);
	    return;
	}
    }
    if (TRACE(SERVER))
	logInfo("FAX ADMIN FROM %s [%s], %s"
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
    usrAdminWd = pass;
    adminAttempts = 0;
    state |= S_PRIVILEGED;
    reply(230, "Administrative privileges established.");
}

/*
 * Terminate login as previous user, if any,
 * resetting state; used when USER command is
 * given or login fails.
 */
void
HylaFAXServer::end_login(void)
{
    if (IS(LOGGEDIN)) {
	uid_t ouid = geteuid();
	seteuid(0);
	seteuid(ouid);
    }
    state &= ~(S_LOGGEDIN|S_PRIVILEGED|S_WAITPASS);
    passWd = "*";
    adminWd = "*";
    usrPassWd = "*";
    usrAdminWd = "*";
}

/*
 * Record logout in wtmp file, cleanup state,
 * and exit with supplied status.
 */
void
HylaFAXServer::dologout(int status)
{
    if (curJobHost != -1) {
	// There is a connection to a job host.  Disconnect it.
	jobHostClient->hangupServer();
	curJobHost = -1;
    }
    if (IS(LOGGEDIN))
	end_login();
    if (trigSpec != "") {
	fxStr emsg;
	cancelTrigger(emsg);
    }
    for (u_int i = 0, n = tempFiles.length(); i < n; i++)
	(void) Sys::unlink(tempFiles[i]);
    if (xferfaxlog != -1)
        Sys::close(xferfaxlog);
    if (clientFd != -1)
	Sys::close(clientFd);
    if (clientFIFOName != "") {
      /* we need to check for the FIFO since dologout() might be called
       * before we are chroot()ed... *sigh*
       */
      if (Sys::isFIFOFile( "/" | clientFIFOName)) {
          Sys::unlink("/" | clientFIFOName);
      } else if (Sys::isFIFOFile( FAX_SPOOLDIR "/" | clientFIFOName)) {
          Sys::unlink( FAX_SPOOLDIR "/" | clientFIFOName);
      }
    }
    for (JobDictIter iter(blankJobs); iter.notDone(); iter++) {
	Job* job = iter.value();
	fxStr file("/" | job->qfile);
	Sys::unlink(file);
    }
    _exit(status);		// beware of flushing buffers after a SIGPIPE
}


