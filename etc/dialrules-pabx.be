! $Id: dialrules-pabx.be 938 2009-09-08 01:05:03Z faxguy $
!
! HylaFAX (tm) Dialing String Processing Rules.
!
! Much of Europe uses full dialing, the LDPrefix Area is always required.
! PBXPre is the access prefix for a CO line when using a PABX.
!
! Belgium: ITU E.123 with IDP=00 and LDP=0:  
!   +32.16.321234 = 016.321234 = 016/321234  (321234 is no longer permitted)
!
! This file describes how to process user-specified dialing strings
! to create two items:
!
! CanonicalNumber: a unique string that is derived from all dialing
! strings to the same destination phone number.  This string is used
! by the fax server for ``naming'' the destination. 
!
! DialString: the string passed to the modem for use in dialing the
! telephone.  This string should be void of any characters that might
! confuse the modem.
!
Area=${AreaCode}		! local area code
Country=${CountryCode}		! local country code
IDPrefix=${InternationalPrefix}	! prefix for placing an international call
LDPrefix=${LongDistancePrefix}	! prefix for placing a long distance call
!
WS=" 	"			! our notion of white space
!
PBXLoc=^[0-9]{1,3}$	! PBX local numbers are 1..3 digits long
PBXPre=61,		! PBX prefix to connect to a CO outside line e.g. "0,"
!
! Convert a phone number to a canonical format:
!
!    +<country><areacode><rest>
!
! by (possibly) stripping off leading dialing prefixes for
! long distance and/or international dialing.
!
CanonicalNumber := [
%.*			=			! strip calling card stuff
[abcABC]		= 2			! convert alpha to numbers
[defDEF]		= 3
[ghiGHI]		= 4
[jklJKL]		= 5
[mnoMNO]		= 6
[prsPRS]		= 7
[tuvTUV]		= 8
[wxyWXY]		= 9
[^+0-9]+		=			! strip white space etc.
${PBXLoc}		= :&   			! PBX short number
^${IDPrefix}		= +			! replace int. dialing code
^${LDPrefix}		= +${Country}		! replace l.d. dialing code
^[^+:]			= +${Country}${Area}&	! otherwise, insert canon form
^: 			= ""
]
!
! Process a dialing string according to local requirements.
! These rules do only one transformation: they convert in-country
! international calls to long-distance calls.
!
! Use ':' prefix to disable IDP LDP substitution.
!
DialString := [
[-${WS}./]+		= ""			! strip syntactic sugar
[abcABC]		= 2			! convert alpha to numbers
[defDEF]		= 3
[ghiGHI]		= 4
[jklJKL]		= 5
[mnoMNO]		= 6
[prsPRS]		= 7
[tuvTUV]		= 8
[wxyWXY]		= 9
${PBXLoc}		= :&			! PBX short number
^${IDPrefix}${Country}  = ${LDPrefix}		! long distance call
^[+]${Country}		= ${LDPrefix}		! long distance call
! ^${LDPrefix}${Area}	= -			! local call avoid rematch
^[+]			= ${IDPrefix}		! international call
! ^-			= ""			! local call cleanup
^[^:]			= ${PBXPre}&		! add PBX prefix
^: 			= ""
]
!
! Strip private information from the dial string which should not be
! displayed to clients.  By default, DisplayNumber is not set.
!
! DisplayNumber := [
! ]
