/*	$Id: Class2Send.c++ 1106 2012-06-18 23:50:58Z faxguy $ */
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
#include <stdio.h>
#include "Sys.h"
#include "Class2.h"
#include "ModemConfig.h"
#include "FaxRequest.h"

/*
 * Send Protocol for Class-2-style modems.
 */

bool
Class2Modem::sendSetup(FaxRequest& req, const Class2Params& dis, fxStr& emsg)
{
    const char* cmdFailed = " (modem command failed)";

    /*
     * PWD and SUB setup don't belong here, they should be done
     * in setupSetupPhaseB at which time we know whether or not
     * the receiver supports them.  However since no status message
     * is defined for T.class2 such that we can determine this
     * information and since some modems will undoubtedly require
     * all session state to be setup prior to the initial call
     * we'll send this stuff to the modem here (for now at least).
     */
    if (req.passwd != "" && pwCmd != "" && !class2Cmd(pwCmd, req.passwd)) {
	emsg = fxStr::format("Unable to send password%s {E204}", cmdFailed);
	return (false);
    }
    if (req.subaddr != "" && saCmd != "" && !class2Cmd(saCmd, req.subaddr)) {
	emsg = fxStr::format("Unable to send subaddress%s {E205}", cmdFailed);
	return (false);
    }
    if (minsp != BR_2400 && !class2Cmd(minspCmd, minsp)) {
	emsg = fxStr::format("Unable to restrict minimum transmit speed to %s%s {E206}",
	    Class2Params::bitRateNames[req.minbr], cmdFailed);
	return (false);
    }
    if (conf.class2DDISCmd != "") {
	if (!class2Cmd(conf.class2DDISCmd, dis, false)) {
	    emsg = fxStr::format("Unable to setup session parameters "
		"prior to call%s {E207}", cmdFailed);
	    return (false);
	}
	params = dis;
    }
    hadHangup = false;
    return (FaxModem::sendSetup(req, dis, emsg));
}

/*
 * Process the response to a dial command.
 */
CallStatus
Class2Modem::dialResponse(fxStr& emsg)
{
    ATResponse r;

    hangupCode[0] = '\0';
    do {
	/*
	 * Use a dead-man timeout since some
	 * modems seem to get hosed and lockup.
	 */
	r = atResponse(rbuf, conf.dialResponseTimeout);

	// Blacklisting (see notes in Class1Send.c++)
	if (strncmp(rbuf, "BLACKLISTED", 11) == 0
		|| strncmp(rbuf, "DELAYED", 7) == 0
		|| strncmp(rbuf, "DIALING DISABLED", 16) == 0) {
	    emsg = "Blacklisted by modem {E010}";
	    return (NOCARRIER);
	}

	switch (r) {
	case AT_ERROR:	    return (ERROR);	// error in dial command
	case AT_BUSY:	    return (BUSY);	// busy signal
	case AT_NOCARRIER:  return (NOCARRIER);	// no carrier detected
	case AT_OK:	    return (NOCARRIER);	// (for AT&T DataPort)
	case AT_NODIALTONE: return (NODIALTONE);// local phone connection hosed
	case AT_NOANSWER:   return (NOANSWER);	// no answer or ring back
	case AT_FHNG:				// Class 2 hangup code
	    emsg = fxStr::format("%s {%s}", hangupCause(hangupCode), hangupCause(hangupCode, true));
	    switch (atoi(hangupCode)) {
	    case 1:	    return (NOANSWER);	// Ring detected w/o handshake
	    case 3:	    return (NOANSWER);	// No loop current (???)
	    case 4:	    return (NOANSWER);	// Ringback detected, no answer
	    case 5:	    return (NOANSWER);	// Ringback ", no answer w/o CED
	    case 10:	    return (NOFCON);	// Unspecified Phase A error
	    case 11:	    return (NOFCON);	// No answer (T.30 timeout)
	    }
	    break;
	case AT_FCON:	    return (OK);	// fax connection
	case AT_TIMEOUT:    return (FAILURE);	// timed out w/o response
	case AT_CONNECT:    return (DATACONN);	// modem thinks data connection
	}
    } while (r == AT_OTHER && isNoise(rbuf));
    return (FAILURE);
}

#define BATCH_FIRST 1
#define BATCH_LAST  2

/*
 * Process the string of session-related information
 * sent to the caller on connecting to a fax machine.
 */
FaxSendStatus
Class2Modem::getPrologue(Class2Params& dis, bool& hasDoc, fxStr& emsg, u_int& batched)
{
    bool gotParams = false;
    hasDoc = false;
    if (batched & BATCH_FIRST) {		// only for the first document
	ATResponse r;
	for (;;) {
	    r  = atResponse(rbuf, conf.t1Timer);
	    switch (r) {
	    case AT_FPOLL:
		hasDoc = true;
		protoTrace("REMOTE has document to POLL");
		break;
	    case AT_FDIS:
		gotParams = parseClass2Capabilities(skipStatus(rbuf), dis, true);
		break;
	    case AT_FNSF:
		recvNSF(NSF(skipStatus(rbuf), conf.class2HexNSF));
		break;
	    case AT_FCSI:
		recvCSI(stripQuotes(skipStatus(rbuf)));
		break;
	    case AT_OK:
		if (gotParams)
		    return (send_ok);
		/* fall thru... */
	    case AT_TIMEOUT:
	    case AT_EMPTYLINE:
	    case AT_NOCARRIER:
	    case AT_NODIALTONE:
	    case AT_NOANSWER:
	    case AT_ERROR:
		processHangup("20");		// Unspecified Phase B error
		/* fall thru... */
	    case AT_FHNG:
		if (r == AT_FHNG) waitFor(AT_OK);
		emsg = fxStr::format("%s {%s}", hangupCause(hangupCode), hangupCause(hangupCode, true));
		return (send_retry);
	    }
	}
    } else {
	/*
	 * We already have the remote DIS.  Class 2 protocol says to now send
	 * +FIS and then +FDT, expecting CONNECT, so we skip this "prologue".
	 */
	return (send_ok);
    }
}

/*
 * Initiate data transfer from the host to the modem when
 * doing a send.  Note that some modems require that we
 * wait for an XON from the modem in response to the +FDT,
 * before actually sending any data.
 */
bool
Class2Modem::dataTransfer()
{
    bool status = false;
    if (xmitWaitForXON) {
	/*
	 * Wait for XON (DC1) from the modem after receiving
	 * CONNECT and before sending page data.  If XON/XOFF
	 * flow control is in use then disable it temporarily
	 * so that we can read the input stream for DC1.
	 */
	if (flowControl == FLOW_XONXOFF)
	    setXONXOFF(FLOW_NONE, getOutputFlow(), ACT_NOW);
    }
    u_short tries = 0;
    ATResponse r;
    do {
	atCmd("AT+FDT", AT_NOTHING, conf.pageStartTimeout);
	do {
	    r = atResponse(rbuf, conf.pageStartTimeout);
	} while (r == AT_OTHER || r > AT_FHNG);	// ignore all Class 2-specific other than +FHNG
    } while (!hadHangup && r == AT_OK && tries++ < 3);
    status = (r == AT_CONNECT);
    if (xmitWaitForXON) {
	if (status) {
	    protoTrace("SEND wait for XON");
	    int c;
	    startTimeout(10*1000);		// 5 seconds *should* be enough
	    while ((c = getModemChar(0)) != EOF) {
		modemTrace("--> [1:%c]", c);
		if (c == DC1)
		    break;
	    }
	    stopTimeout("waiting for XON before sending page data");
	    status = (c == DC1);
	}
	if (flowControl == FLOW_XONXOFF) {
	    FlowControl oiFlow = getInputFlow();
	    setXONXOFF(oiFlow, getOutputFlow(), ACT_NOW);
	}
    }
    return (status);
}

static bool
pageInfoChanged(const Class2Params& a, const Class2Params& b)
{
    return (a.vr != b.vr || a.wd != b.wd || a.ln != b.ln || a.df != b.df || a.br != b.br);
}

void
Class2Modem::getDataStats(FaxSetup* setupinfo)
{
    return;
}

/*
 * Send the specified document using the supplied
 * parameters.  The pph is the post-page-handling
 * indicators calculated prior to intiating the call.
 */
FaxSendStatus
Class2Modem::sendPhaseB(TIFF* tif, Class2Params& next, FaxMachineInfo& info,
    fxStr& pph, fxStr& emsg, u_int& batched)
{
    int ntrys = 0;			// # retraining/command repeats
    u_int ppm, previousppm = 0;

    setDataTimeout(180, next.br);	// 180 seconds for 1024 byte writes, increased for potential ECM delays
    hangupCode[0] = '\0';

    bool transferOK;
    bool morePages = false;
    do {
	transferOK = false;
	if (abortRequested())
	     goto failed;
	/*
	 * Check the next page to see if the transfer
	 * characteristics change.  If so, update the
	 * current T.30 session parameters.
	 */
	if (pageInfoChanged(params, next)) {
	    if (!class2Cmd(disCmd, next, false)) {
		emsg = "Unable to set session parameters {E208}";
		break;
	    }
	    params = next;
	}
	if (dataTransfer() && sendPage(tif, decodePageChop(pph, params))) {
	    /*
	     * Page transferred, process post page response from
	     * remote station (XXX need to deal with PRI requests).).
	     */
	    morePages = !TIFFLastDirectory(tif);
	    if (!decodePPM(pph, ppm, emsg))
		goto failed;

	    if (ppm == PPM_EOP && !(batched & BATCH_LAST)) {
		ppm = PPM_EOM;
		// this should force us to resend disCmd, since some modems don't remember
		params.vr = (u_int) -1;
	    }

	    tracePPM("SEND send", ppm);
	    u_int ppr;
	    if (pageDone(ppm, ppr)) {
		tracePPR("SEND recv", ppr);
		switch (ppr) {
		case PPR_MCF:		// page good
		case PPR_PIP:		// page good, interrupt requested
		case PPR_RTP:		// page good, retrain requested
                ignore:
		    countPage();	// bump page count
		    notifyPageSent(tif);// update server
		    if (pph[4] == 'Z')
			pph.remove(0,4+5+1);	// discard page-chop+handling
		    else
			pph.remove(0,5);	// discard page-handling info
		    ntrys = 0;
		    if (morePages) {
			if (ppr == PPR_PIP) {
			    emsg = "Procedure interrupt (operator intervention) {E280}";
			    goto failed;
			}
			if (!TIFFReadDirectory(tif)) {
			    emsg = "Problem reading document directory {E302}";
			    goto failed;
			}
			if (ppr == PPR_MCF) {
			    /*
			     * The session parameters cannot change except following
			     * the reception of an RTN or RTP signal or the transmission
			     * of an EOM signal.
			     *
			     * Since we did not receive RTN or RTP, and since batching (EOM)
			     * triggers retraining in other ways, we require that the
			     * next page have the same characteristics as this page.
			     */
			    next = params;
			}
		    }
		    transferOK = true;
		    break;
		case PPR_RTN:		// page bad, retrain requested
                    switch( conf.rtnHandling ){
		    case RTN_RETRANSMITIGNORE:
			if (ntrys < 2) break;
                    case RTN_IGNORE:
                        goto ignore; // ignore error and trying to send next page
                    case RTN_GIVEUP:
                        emsg = "Unable to transmit page (giving up after RTN) {E281}";
                        goto failed; // "over and out"
                    }
                    // case RTN_RETRANSMIT
		    if (++ntrys >= 3) {
			emsg = "Unable to transmit page (giving up after 3 attempts) {E282}";
			break;
		    }
		    if (params.br == BR_2400) {
			emsg = "Unable to transmit page (NAK at all possible signalling rates) {E283}";
			break;
		    }
		    next.br--;
		    morePages = true;	// retransmit page
		    transferOK = true;
		    break;
		case PPR_PIN:		// page bad, interrupt requested
		    emsg = "Unable to transmit page (NAK with operator intervention) {E284}";
		    goto failed;
		default:
		    emsg = "Modem protocol error (unknown post-page response) {E285}";
		    break;
		}
	    } else {
		/*
		 * We received no PPR.
		 */
		if (ppm == PPM_EOM && (batched & BATCH_FIRST)) {
		    emsg = "Batching protocol error {E286}";
		    protoTrace("The destination appears to not support batching.");
		    return (send_batchfail);
		}
	    }
	} else {
	    /*
	     * We were unable to negotiate settings and transfer page image data.
	     */
	    if (previousppm == PPM_EOM) {
		emsg = "Batching protocol error {E286}";
		protoTrace("The destination appears to not support batching.");
		return (send_batchfail);
	    }
	}
	previousppm = ppm;
    } while (transferOK && morePages && !hadHangup);
    if (!transferOK) {
	if (emsg == "") {
	    if (hangupCode[0])
		emsg = fxStr::format("%s {%s}", hangupCause(hangupCode), hangupCause(hangupCode, true));
	    else
		emsg = "Communication failure during Phase B/C {E287}";
	}
	sendAbort();			// terminate session
    } else if (hadHangup && morePages) {
	/*
	 * Modem hung up before the transfer completed (e.g. PPI
	 * modems which get confused when they receive RTN and return
	 * +FHNG:0).  Setup an error return so that the job will
	 * be retried.
	 */
	transferOK = false;
	emsg = "Communication failure during Phase B/C (modem protocol botch) {E288}";
    }
    return (transferOK ? send_ok : send_retry);
failed:
    sendAbort();
    return (send_failed);
}

/*
 * Send one page of data to the modem, imaging any
 * tag line that is configured.  We also implement
 * page chopping based on the calculation done by
 * faxq during document preparation.
 *
 * Note that we read an entire page of encoded data
 * into memory before sending it to the modem.  This
 * is done to avoid timing problems when the document
 * is comprised of multiple strips.
 */
bool
Class2Modem::sendPageData(TIFF* tif, u_int pageChop)
{
    bool rc = true;

    /* For debugging purposes we may want to write the image-data to file. */
    if (conf.saverawimage) imagefd = Sys::open("/tmp/out.fax", O_RDWR|O_CREAT|O_EXCL);

    tstrip_t nstrips = TIFFNumberOfStrips(tif);
    if (nstrips > 0) {

	/*
	 * RTFCC may mislead us here, so we temporarily
	 * adjust params.
	 */
	Class2Params newparams = params;
	uint16 compression;
	TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
	if (compression != COMPRESSION_CCITTFAX4) {
	    uint32 g3opts = 0;
	    TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &g3opts);
	    if ((g3opts & GROUP3OPT_2DENCODING) == DF_2DMR)
		params.df = DF_2DMR;
	    else
		params.df = DF_1DMH;
	} else
	    params.df = DF_2DMMR;

	/*
	 * Correct bit order of data if not what modem expects.
	 */
	uint16 fillorder;
	TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &fillorder);
	const u_char* bitrev =
	    TIFFGetBitRevTable(fillorder != sendFillOrder);
	/*
	 * Setup tag line processing.
	 */
	bool doTagLine = setupTagLineSlop(params);
	u_int ts = getTagLineSlop();
	/*
	 * Calculate total amount of space needed to read
	 * the image into memory (in its encoded format).
	 */
	TIFFSTRIPBYTECOUNTS* stripbytecount;
	(void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &stripbytecount);
	tstrip_t strip;
	u_long totdata = 0;
	for (strip = 0; strip < nstrips; strip++)
	    totdata += stripbytecount[strip];
	/*
	 * Read the image into memory.
	 */
	u_char* data = new u_char[totdata+ts];
	u_int off = ts;			// skip tag line slop area
	for (strip = 0; strip < nstrips; strip++) {
	    uint32 sbc = stripbytecount[strip];
	    if (sbc > 0 && TIFFReadRawStrip(tif, strip, data+off, sbc) >= 0)
		off += (u_int) sbc;
	}
	totdata -= pageChop;		// deduct trailing white space not sent
	/*
	 * Image the tag line, if intended, and then
	 * pass the data to the modem, filtering DLE's
	 * and being careful not to get hung up.
	 */
	u_char* dp;
	if (doTagLine) {
	    u_long totbytes = totdata;
	    dp = imageTagLine(data+ts, fillorder, params, totbytes);
	    totdata = (params.df == DF_2DMMR) ? totbytes : totdata+ts - (dp-data);
	} else
	    dp = data;

	uint32 rows = 0;
	if (conf.softRTFCC && !conf.class2RTFCC && params.df != newparams.df) {
	    switch (params.df) {
		case DF_1DMH:
		    protoTrace("Reading MH-compressed image file");
		    break;
		case DF_2DMR:
		    protoTrace("Reading MR-compressed image file");
		    break;
		case DF_2DMMR:
		    protoTrace("Reading MMR-compressed image file");
		    break;
	    }
	    dp = convertPhaseCData(dp, totdata, fillorder, params, newparams, rows);
	    params = newparams;		// revert back
	}

        /*
         * correct broken Phase C (T.4) data if necessary
         */
        if (params.df < DF_2DMMR) {
	    correctPhaseCData(dp, totdata, fillorder, params, rows);
	} else if (params.df == DF_JBIG) {
	    // JBIG needs the data bit-reversed as we get it backwards from the library
	    TIFFReverseBits(dp, totdata);
	}

	if (imagefd > 0) Sys::write(imagefd, (const char*) dp, (u_int) totdata);
	beginTimedTransfer();
	rc = putModemDLEData(dp, (u_int) totdata, bitrev, getDataTimeout(), conf.doPhaseCDebug);
	endTimedTransfer();
	protoTrace("SENT %u bytes of data", totdata);
    }
    if (imagefd > 0) {
	Sys::close(imagefd);
	imagefd = 0;
    }
    return (rc);
}

/*
 * Send RTC to terminate a page.
 */
bool
Class2Modem::sendRTC(Class2Params params)
{
    if (params.df > DF_2DMR) return (true);	// nothing to do

    // these are intentionally reverse-encoded in order to keep
    // rtcRev and bitrev in sendPage() in agreement
    static const u_char RTC1D[9] =
	{ 0x00,0x08,0x80,0x00,0x08,0x80,0x00,0x08,0x80 };
    static const u_char RTC2D[10] =
	{ 0x00,0x18,0x00,0x03,0x60,0x00,0x0C,0x80,0x01,0x30 };
    protoTrace("SEND %s RTC", params.is2D() ? "2D" : "1D");
    if (params.is2D())
	return putModemDLEData(RTC2D, sizeof (RTC2D), rtcRev, getDataTimeout());
    else
	return putModemDLEData(RTC1D, sizeof (RTC1D), rtcRev, getDataTimeout());
}

/*
 * Abort an active Class 2 session.
 */
void
Class2Modem::sendAbort()
{
    if (!hadHangup)
	(void) atCmd(abortCmd);
}
