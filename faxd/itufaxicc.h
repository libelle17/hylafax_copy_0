/* $Id: itufaxicc.h 965 2009-12-22 06:07:31Z faxguy $ */
//  Copyright (C) 2009 Marti Maria
//
// Permission is hereby granted, free of charge, to any person obtaining 
// a copy of this software and associated documentation files (the "Software"), 
// to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the Software 
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in 
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO 
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE 
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef __itufaxicc_H

#include <stdio.h>
#include <tiffio.h>

/*
This function accepts a single JPEG image oat src and converts it from ITULAB colorspace sRGB.  
In the source image the CIE Standard Illuminant D50 is used in the colour image data as 
specified in ITU-T Rec. T.42.
In the source image the colour image data are represented using the default gamut range
as specified in ITU-T Rec. T.42.  The function operation fills dst stream and returns true 
if it succeeded or false if an error occurred.  If an error occurred, then the error message 
should be found in emsg.
*/

bool convertJPEGfromITULAB(FILE* src, FILE* dst, char* emsg, size_t max_emsg_bytes );


/*
This function accepts a single sRGB JPEG image at src and converts it to ITULAB colorspace.  
In the destination image the CIE Standard Illuminant D50 is used in the colour image data 
as specified in ITU-T Rec. T.42. In the destination image the colour image data are 
represented using the default gamut range as specified in ITU-T Rec. T.42.  
The G3FAX0 app header is added from the JPEG data as instructed by ITU T.4 Annex E.  
The function operation fills dst stream and returns true if it succeeded or false if an 
error occurred.  If an error occurred, then the error message should be found in emsg.
*/

bool convertJPEGtoITULAB( FILE* src, FILE* dst, char* emsg, size_t max_emsg_bytes );


/*
This function accepts a single raw RGB image at src and converts it to ITULAB colorspace.
In the destination image the CIE Standard Illuminant D50 is used in the colour image data
as specified in ITU-T Rec. T.42. In the destination image the colour image data are
represented using the default gamut range as specified in ITU-T Rec. T.42.
The G3FAX0 app header is added from the JPEG data as instructed by ITU T.4 Annex E.
The function operation fills dst stream and returns true if it succeeded or false if an
error occurred.  If an error occurred, then the error message should be found in emsg.
*/

bool convertRawRGBtoITULAB( tdata_t src, tsize_t srclen, uint32 width, uint32 height, FILE* dst, char* emsg, size_t max_emsg_bytes );


#define __itufaxicc_H

#endif

