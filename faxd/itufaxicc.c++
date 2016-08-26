/* $Id: itufaxicc.c++ 1082 2012-01-31 00:58:35Z faxguy $ */
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


// uncomment this symbol to generate a minimal test application *****************
//#define MAKE_DEMO 1
//#define MAKE_DEMO2 1
// ******************************************************************************

#include "config.h"
#if defined(HAVE_JPEG) && ( defined(HAVE_LCMS) || defined(HAVE_LCMS2) )

#if defined(HAVE_LCMS)
#include "lcms.h"
#else
#include "lcms2.h"
#endif
#include "itufaxicc.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <setjmp.h>

#define XMD_H
extern "C" {
#include "jpeglib.h"
};

// This is the error catcher
static char ErrorMessage[JMSG_LENGTH_MAX];
static jpeg_error_mgr ErrorHandler;
static jmp_buf State;

// Error handler for IJG library
static
void jpg_error_exit(j_common_ptr cinfo)
{   
	(*cinfo->err->format_message) (cinfo, ErrorMessage);  
	longjmp(State, 1);
}

// Error handler for lcms library
static
int lcms_error_exit(int ErrorCode, const char *ErrorText)
{
	strncpy(ErrorMessage, ErrorText, JMSG_LENGTH_MAX-1);
	longjmp(State, 1);
}


// Build a profile for decoding ITU T.42/Fax JPEG streams. 
// The profile has an additional ability in the input direction of
// gamut compress values between 85 < a < -85 and -75 < b < 125. This conforms
// the default range for ITU/T.42 -- See RFC 2301, section 6.2.3 for details

//  L*  =   [0, 100]
//  a*  =   [–85, 85]
//  b*  =   [–75, 125]


// These functions does convert the encoding of ITUFAX to floating point
// and vice-versa. No gamut mapping is performed yet.

#if defined(HAVE_LCMS)
static
void ITU2Lab(WORD In[3], LPcmsCIELab Lab)
{
	Lab -> L = (double) In[0] / 655.35;
	Lab -> a = (double) 170.* (In[1] - 32768.) / 65535.;
	Lab -> b = (double) 200.* (In[2] - 24576.) / 65535.;
}

static
void Lab2ITU(LPcmsCIELab Lab, WORD Out[3])
{
	Out[0] = (WORD) floor((double) (Lab -> L / 100.)* 65535. );
	Out[1] = (WORD) floor((double) (Lab -> a / 170.)* 65535. + 32768. );
	Out[2] = (WORD) floor((double) (Lab -> b / 200.)* 65535. + 24576. );
}
#else
static
void ITU2Lab(const cmsUInt16Number In[3], cmsCIELab* Lab)
{
	Lab -> L = (double) In[0] / 655.35;
	Lab -> a = (double) 170.* (In[1] - 32768.) / 65535.;
	Lab -> b = (double) 200.* (In[2] - 24576.) / 65535.;
}
 
static
void Lab2ITU(const cmsCIELab* Lab, cmsUInt16Number Out[3])
{
	Out[0] = (cmsUInt16Number) floor((double) (Lab -> L / 100.)* 65535. );
	Out[1] = (cmsUInt16Number) floor((double) (Lab -> a / 170.)* 65535. + 32768. );
	Out[2] = (cmsUInt16Number) floor((double) (Lab -> b / 200.)* 65535. + 24576. );
}
#endif

// These are the samplers-- They are passed as callbacks to cmsSample3DGrid()
// then, cmsSample3DGrid() will sweel whole Lab gamut calling these functions
// once for each node. In[] will contain the Lab PCS value to convert to ITUFAX
// on PCS2ITU, or the ITUFAX value to convert to Lab in ITU2PCS
// You can change the number of sample points if desired, the algorithm will
// remain same. 33 points gives good accurancy, but you can reduce to 22 or less
// is space is critical

#define GRID_POINTS 33

#if defined(HAVE_LCMS)
static
int PCS2ITU(register WORD In[], register WORD Out[], register LPVOID Cargo)
{      
	cmsCIELab Lab;

	cmsLabEncoded2Float(&Lab, In);    
	cmsClampLab(&Lab, 85, -85, 125, -75);    // This function does the necessary gamut remapping  
	Lab2ITU(&Lab, Out);
	return TRUE;
}

static
int ITU2PCS(register WORD In[], register WORD Out[], register LPVOID Cargo)
{   
	cmsCIELab Lab;

	ITU2Lab(In, &Lab);
	cmsFloat2LabEncoded(Out, &Lab);    
	return TRUE;
}

#else
#define UTILS_UNUSED_PARAMETER(x) ((void)x)

static
int PCS2ITU(register const cmsUInt16Number In[], register cmsUInt16Number Out[], register void*  Cargo)
{   
	cmsCIELab Lab;

	cmsLabEncoded2Float(&Lab, In);
	cmsDesaturateLab(&Lab, 85, -85, 125, -75);	// This function does the necessary gamut remapping
	Lab2ITU(&Lab, Out);
	return TRUE;

    UTILS_UNUSED_PARAMETER(Cargo);
}

static
int ITU2PCS( register const cmsUInt16Number In[], register cmsUInt16Number Out[], register void*  Cargo)
{   
	cmsCIELab Lab;

	ITU2Lab(In, &Lab);
	cmsFloat2LabEncoded(Out, &Lab);
	return TRUE;

    UTILS_UNUSED_PARAMETER(Cargo);
}
#endif

// This function does create the virtual input profile, which decoded ITU  
// to the profile connection space
#if defined(HAVE_LCMS)
static
cmsHPROFILE CreateITU2PCS_ICC(void)
{
	LPLUT AToB0 = cmsAllocLUT();
	if (AToB0 == NULL) return NULL;

	cmsHPROFILE hProfile = _cmsCreateProfilePlaceholder();
	if (hProfile == NULL) {
		cmsFreeLUT(AToB0);
		return NULL;
	}

	cmsAlloc3DGrid(AToB0, GRID_POINTS, 3, 3);   
	cmsSample3DGrid(AToB0, ITU2PCS, NULL, 0);
	cmsAddTag(hProfile, icSigAToB0Tag, AToB0);                              
	cmsSetColorSpace(hProfile, icSigLabData);
	cmsSetPCS(hProfile, icSigLabData);
	cmsSetDeviceClass(hProfile, icSigColorSpaceClass);      
	cmsFreeLUT(AToB0);

	return hProfile;
}
#else
static
cmsHPROFILE CreateITU2PCS_ICC(void)
{
	cmsHPROFILE hProfile;
	cmsPipeline* AToB0;
	cmsStage* ColorMap;

	AToB0 = cmsPipelineAlloc(0, 3, 3);
	if (AToB0 == NULL) return NULL;

	ColorMap = cmsStageAllocCLut16bit(0, GRID_POINTS, 3, 3, NULL);
	if (ColorMap == NULL) return NULL;
        
    cmsPipelineInsertStage(AToB0, cmsAT_BEGIN, ColorMap);
	cmsStageSampleCLut16bit(ColorMap, ITU2PCS, NULL, 0);

	hProfile = cmsCreateProfilePlaceholder(0);
	if (hProfile == NULL) {
		cmsPipelineFree(AToB0);
		return NULL;
	}

	cmsWriteTag(hProfile, cmsSigAToB0Tag, AToB0);
	cmsSetColorSpace(hProfile, cmsSigLabData);
	cmsSetPCS(hProfile, cmsSigLabData);
	cmsSetDeviceClass(hProfile, cmsSigColorSpaceClass);
	cmsPipelineFree(AToB0);

	return hProfile;
}
#endif

// This function does create the virtual output profile, with the 
// necessary gamut mapping 
#if defined(HAVE_LCMS)
static
cmsHPROFILE CreatePCS2ITU_ICC(void)
{
	LPLUT BToA0 = cmsAllocLUT();
	if (BToA0 == NULL) return NULL;

	cmsHPROFILE hProfile = _cmsCreateProfilePlaceholder();
	if (hProfile == NULL) {
		cmsFreeLUT(BToA0);
		return NULL;
	}

	cmsAlloc3DGrid(BToA0, GRID_POINTS, 3, 3);
	cmsSample3DGrid(BToA0, PCS2ITU, NULL, 0);
	cmsAddTag(hProfile, icSigBToA0Tag, BToA0);                              
	cmsSetColorSpace(hProfile, icSigLabData);
	cmsSetPCS(hProfile, icSigLabData);
	cmsSetDeviceClass(hProfile, icSigColorSpaceClass);      
	cmsFreeLUT(BToA0);

	return hProfile;
}
#else
static
cmsHPROFILE CreatePCS2ITU_ICC(void)
{
    cmsHPROFILE hProfile;
    cmsPipeline* BToA0;
    cmsStage* ColorMap;

    BToA0 = cmsPipelineAlloc(0, 3, 3);
    if (BToA0 == NULL) return NULL;

    ColorMap = cmsStageAllocCLut16bit(0, GRID_POINTS, 3, 3, NULL);
    if (ColorMap == NULL) return NULL;

    cmsPipelineInsertStage(BToA0, cmsAT_BEGIN, ColorMap);
    cmsStageSampleCLut16bit(ColorMap, PCS2ITU, NULL, 0);

    hProfile = cmsCreateProfilePlaceholder(0);
    if (hProfile == NULL) {
        cmsPipelineFree(BToA0);
        return NULL;
    }

    cmsWriteTag(hProfile, cmsSigBToA0Tag, BToA0);
    cmsSetColorSpace(hProfile, cmsSigLabData);
    cmsSetPCS(hProfile, cmsSigLabData);
    cmsSetDeviceClass(hProfile, cmsSigColorSpaceClass);

    cmsPipelineFree(BToA0);

    return hProfile;
}
#endif

/*
Definition of the APPn Markers Defined for continuous-tone G3FAX

The application code APP1 initiates identification of the image as 
a G3FAX application and defines the spatial resolution and subsampling. 
This marker directly follows the SOI marker. The data format will be as follows:

X'FFE1' (APP1), length, FAX identifier, version, spatial resolution.

The above terms are defined as follows:

Length: (Two octets) Total APP1 field octet count including the octet count itself, but excluding the APP1
marker.

FAX identifier: (Six octets) X'47', X'33', X'46', X'41', X'58', X'00'. This X'00'-terminated string "G3FAX"
uniquely identifies this APP1 marker.

Version: (Two octets) X'07CA'. This string specifies the year of approval of the standard, for identification
in the case of future revision (for example, 1994).

Spatial Resolution: (Two octets) Lightness pixel density in pels/25.4 mm. The basic value is 200. Allowed values are
100, 200, 300, 400, 600 and 1200 pels/25.4 mm, with square (or equivalent) pels.

NOTE – The functional equivalence of inch-based and mm-based resolutions is maintained. For example, the 200 × 200
*/

static
bool IsITUFax(jpeg_saved_marker_ptr ptr)
{
	while (ptr) 
	{
		if (ptr -> marker == (JPEG_APP0 + 1) && ptr -> data_length > 5)
		{
			const char* data = (const char*) ptr -> data;

			if (strcmp(data, "G3FAX") == 0) return TRUE;                                
		}

		ptr = ptr -> next;
	}

	return FALSE;
}


static
void SetITUFax(j_compress_ptr cinfo)
{    
	unsigned char Marker[] = "G3FAX\x00\0x07\xCA\x00\xC8";

	jpeg_write_marker(cinfo, (JPEG_APP0 + 1), Marker, 10);      
}


// Creates a descompressor for the input image
static
bool OpenInput(FILE* InFile, j_decompress_ptr Decompressor, J_COLOR_SPACE output_color_space)
{

	Decompressor ->err  = jpeg_std_error(&ErrorHandler);
	ErrorHandler.error_exit      = jpg_error_exit;
	ErrorHandler.output_message  = jpg_error_exit;

	jpeg_create_decompress(Decompressor);
	jpeg_stdio_src(Decompressor, InFile);

	// Needed in the case of ITU Lab input  
	int m;
	for (m = 0; m < 16; m++)
		jpeg_save_markers(Decompressor, JPEG_APP0 + m, 0xFFFF);

	// Rewind the file
	if (fseek(InFile, 0, SEEK_SET) != 0) return false;

	// Take the header
	jpeg_read_header(Decompressor, TRUE);

	// Now we can force the input colorspace. 
	// For ITULab, we will use YCbCr as a "don't touch" marker
	Decompressor -> out_color_space = output_color_space;  
	return true;
}

// Creates a compressor for output image
static
bool OpenOutput(FILE* OutFile, j_compress_ptr Compressor, J_COLOR_SPACE input_color_space)
{   
	Compressor -> err = jpeg_std_error(&ErrorHandler);
	ErrorHandler.error_exit      = jpg_error_exit;
	ErrorHandler.output_message  = jpg_error_exit;

	jpeg_create_compress(Compressor);
	jpeg_stdio_dest(Compressor, OutFile);  

	// Force the destination color space
	Compressor ->in_color_space =  input_color_space;    
	Compressor ->input_components = 3;

	jpeg_set_defaults(Compressor);

	return true;
}

// Apply a color transform to convert form sRGB to ITULab or ITULab to sRGB
static
bool DoTransform(cmsHTRANSFORM hXForm, j_decompress_ptr Decompressor, j_compress_ptr Compressor, bool setitu)
{       
	JSAMPROW ScanLineIn;
	JSAMPROW ScanLineOut;


	// We need to keep those
	Compressor ->density_unit = Decompressor ->density_unit;
	Compressor ->X_density    = Decompressor ->X_density;
	Compressor ->Y_density    = Decompressor ->Y_density;

	jpeg_start_decompress(Decompressor);
	jpeg_start_compress(Compressor, TRUE);

	if (setitu) {
		SetITUFax(Compressor);
	}

	ScanLineIn  = (JSAMPROW) malloc(Decompressor ->output_width * Decompressor ->num_components);
	if (ScanLineIn == NULL) return false;

	ScanLineOut = (JSAMPROW) malloc(Compressor ->image_width * Compressor ->num_components);
	if (ScanLineOut == NULL) {
		free(ScanLineIn);
		return false;
	}

	while (Decompressor->output_scanline < Decompressor ->output_height) {

		jpeg_read_scanlines(Decompressor, &ScanLineIn, 1);

		cmsDoTransform(hXForm, ScanLineIn, ScanLineOut, Decompressor ->output_width);

		jpeg_write_scanlines(Compressor, &ScanLineOut, 1);
	}

	free(ScanLineIn); 
	free(ScanLineOut);

	jpeg_finish_decompress(Decompressor);
	jpeg_finish_compress(Compressor);

	return TRUE;
}

// Apply a color transform to convert from sRGB to ITULab
static
bool DoCompression(cmsHTRANSFORM hXForm, JSAMPLE* src, tsize_t srclen, uint32 width, j_compress_ptr Compressor)
{       
	JSAMPROW ScanLineOut;
	jpeg_start_compress(Compressor, TRUE);

	SetITUFax(Compressor);

	JSAMPROW ScanLineIn;
	ScanLineOut = (JSAMPROW) malloc(Compressor ->image_width * Compressor ->num_components);
	if (ScanLineOut == NULL) {
		return false;
	}

	tsize_t pos = 0;
	while (pos < srclen) {

		ScanLineIn = src + pos; 

		cmsDoTransform(hXForm, ScanLineIn, ScanLineOut, width);

		jpeg_write_scanlines(Compressor, &ScanLineOut, 1);

		pos += width*3;
	}

	free(ScanLineOut);

	jpeg_finish_compress(Compressor);

	return TRUE;
}


/*
This function accepts a single JPEG image at src and converts it from ITULAB colorspace sRGB.  
In the source image the CIE Standard Illuminant D50 is used in the colour image data as 
specified in ITU-T Rec. T.42.
In the source image the colour image data are represented using the default gamut range
as specified in ITU-T Rec. T.42.  The function operation fills dst stream and returns true 
if it succeeded or false if an error occurred.  If an error occurred, then the error message 
should be found in emsg.
*/

bool convertJPEGfromITULAB(FILE* src, FILE* dst, char* emsg, size_t max_emsg_bytes )
{
	jpeg_decompress_struct Decompressor;
	jpeg_compress_struct   Compressor;

#if defined(HAVE_LCMS)
	cmsSetErrorHandler(lcms_error_exit);
#endif
	ErrorMessage[0] = 0;
	*emsg = 0;

	if (setjmp(State)) {

		strncpy(emsg, ErrorMessage, max_emsg_bytes-1);
		emsg[max_emsg_bytes-1] = 0;
		if (emsg[0] == 0) emsg = (char*) "Unspecified libjpeg error.";
		return false;
	}


	// Create input decompressor. 
	if (!OpenInput(src, &Decompressor, JCS_YCbCr)) {
		if (emsg[0] == 0) emsg = (char*) "Could not create input decompressor.";
		return false;
	}

	// Sanity check
	if (!IsITUFax(Decompressor.marker_list)) {
		if (emsg[0] == 0) emsg = (char*) "Is not ITUFAX.";
		return false;
	}

	// Create dest compressor
	if (!OpenOutput(dst, &Compressor, JCS_RGB)) {
		if (emsg[0] == 0) emsg = (char*) "Could not create output compressor.";
		return false;
	}

	// Copy size, resolution, etc
	jpeg_copy_critical_parameters(&Decompressor, &Compressor);
	Compressor.in_color_space = JCS_RGB;

	cmsHPROFILE hITUFax = CreateITU2PCS_ICC();
	cmsHPROFILE hsRGB   = cmsCreate_sRGBProfile();

	cmsHTRANSFORM hXform = cmsCreateTransform( hITUFax, TYPE_Lab_8, hsRGB, TYPE_RGB_8, INTENT_PERCEPTUAL, cmsFLAGS_NOWHITEONWHITEFIXUP);

	if (!DoTransform(hXform, &Decompressor, &Compressor, false)) {
		if (emsg[0] == 0) emsg = (char*) "Could not perform transform.";
		return false;
	}

	cmsDeleteTransform(hXform);
	cmsCloseProfile(hITUFax);
	cmsCloseProfile(hsRGB);

	jpeg_destroy_decompress(&Decompressor);
	jpeg_destroy_compress(&Compressor);

	return true;
}


/*
This function accepts a single sRGB JPEG image at src and converts it to ITULAB colorspace 
In the destination image the CIE Standard Illuminant D50 is used in the colour image data 
as specified in ITU-T Rec. T.42. In the destination image the colour image data are 
represented using the default gamut range as specified in ITU-T Rec. T.42.  
The G3FAX0 app header is added from the JPEG data as instructed by ITU T.4 Annex E.  
The function operation fills dst stream and returns true if it succeeded or false if an 
error occurred.  If an error occurred, then the error message should be found in emsg.
*/

bool convertJPEGtoITULAB( FILE* src, FILE* dst, char* emsg, size_t max_emsg_bytes )
{

	jpeg_decompress_struct Decompressor;
	jpeg_compress_struct   Compressor;

#if defined(HAVE_LCMS)
	cmsSetErrorHandler(lcms_error_exit);
#endif
	ErrorMessage[0] = 0;
	*emsg = 0;

	if (setjmp(State)) {
		strncpy(emsg, ErrorMessage, max_emsg_bytes-1);
		emsg[max_emsg_bytes-1] = 0;
		return false;
	}

	if (!OpenOutput(dst, &Compressor, JCS_YCbCr)) return false;
	if (!OpenInput(src, &Decompressor, JCS_RGB)) return false;

	jpeg_copy_critical_parameters(&Decompressor, &Compressor);

	cmsHPROFILE hITUFax = CreatePCS2ITU_ICC();
	cmsHPROFILE hsRGB   = cmsCreate_sRGBProfile();

	cmsHTRANSFORM hXform = cmsCreateTransform(hsRGB, TYPE_RGB_8, hITUFax, TYPE_Lab_8,  INTENT_PERCEPTUAL, cmsFLAGS_NOWHITEONWHITEFIXUP);

	if (!DoTransform(hXform, &Decompressor, &Compressor, true)) return false;

	cmsDeleteTransform(hXform);
	cmsCloseProfile(hITUFax);
	cmsCloseProfile(hsRGB);

	jpeg_destroy_decompress(&Decompressor);
	jpeg_destroy_compress(&Compressor);

	return true;
}

/*
This function accepts a single raw RGB image at src and converts it to ITULAB colorspace.  
In the destination image the CIE Standard Illuminant D50 is used in the colour image data 
as specified in ITU-T Rec. T.42. In the destination image the colour image data are 
represented using the default gamut range as specified in ITU-T Rec. T.42.  
The G3FAX0 app header is added from the JPEG data as instructed by ITU T.4 Annex E.  
The function operation fills dst stream and returns true if it succeeded or false if an 
error occurred.  If an error occurred, then the error message should be found in emsg.
*/

bool convertRawRGBtoITULAB( tdata_t src, tsize_t srclen, uint32 width, uint32 height, FILE* dst, char* emsg, size_t max_emsg_bytes )
{
	jpeg_compress_struct   Compressor;

#if defined(HAVE_LCMS)
	cmsSetErrorHandler(lcms_error_exit);
#endif
	ErrorMessage[0] = 0;
	*emsg = 0;

	if (setjmp(State)) {
		strncpy(emsg, ErrorMessage, max_emsg_bytes-1);
		emsg[max_emsg_bytes-1] = 0;
		return false;
	}

	if (!OpenOutput(dst, &Compressor, JCS_YCbCr)) return false;

	// size, resolution, etc
	Compressor.image_width = width;
	Compressor.image_height = height;
	Compressor.input_components = 3;
	Compressor.in_color_space = JCS_YCbCr;		// lcms takes it from RGB to ITULAB (YCbCr)

	cmsHPROFILE hITUFax = CreatePCS2ITU_ICC();
	cmsHPROFILE hsRGB   = cmsCreate_sRGBProfile();

	cmsHTRANSFORM hXform = cmsCreateTransform(hsRGB, TYPE_RGB_8, hITUFax, TYPE_Lab_8,  INTENT_PERCEPTUAL, cmsFLAGS_NOWHITEONWHITEFIXUP);

	if (!DoCompression(hXform, (JSAMPLE*) src, srclen, width, &Compressor)) return false;

	cmsDeleteTransform(hXform);
	cmsCloseProfile(hITUFax);
	cmsCloseProfile(hsRGB);

	jpeg_destroy_compress(&Compressor);

	return true;
}

#ifdef MAKE_DEMO
int main()
{

	FILE*in, *out;
	char kk[256];


	printf("Demo of IT/Lab to sRGB library. It will convert a JPEG called 'ramps.jpg'\n");
	printf("to ITU/Lab itu2.jpeg, and then back to sRGB into a file caled 'srgb.jpg'\n");
	printf("Any error will be printed on screen\n");

	// sRGB to ITU  
	in = fopen("ramps.jpg", "rb");
	if (in == NULL) {
	   printf("Unable to open 'ramps.jpg'!\n");
	   return 1;
	}

	out = fopen("itu2.jpg", "wb");
	convertJPEGtoITULAB(in, out, kk, 256);
	fclose(in); fclose(out);


	printf("ramps.jpg ==> itu2.jpg Status='%s'\n", kk);

	// Back to sRGB
	in = fopen("itu2.jpg", "rb");
	out = fopen("srgb.jpg", "wb");   
	convertJPEGfromITULAB(in, out, kk, 256 );
	fclose(in); fclose(out);

	printf("itu2.jpg ==> srgb.jpg Status='%s'\n", kk);

	printf("Done!\n");
	return 0;
}
#endif

#ifdef MAKE_DEMO2
int main()
{
	FILE *in, *out;
	char kk[256];

	printf("Demo of IT/Lab library.\n");
	printf("It will convert a single-strip uncompressed RGB TIFF\n");
	printf("called 'ramps.tif' to ITU/Lab itu2.jpeg.'\n");
	printf("Any error will be printed on screen\n");

	// sRGB to ITU  
	TIFF* tif = TIFFOpen("ramps.tif", "r");
	if (!tif) {
	   printf("Unable to open 'ramps.tif'!\n");
	   return 1;
	}
	tstrip_t nstrips = TIFFNumberOfStrips(tif);
	tsize_t stripsize = TIFFStripSize(tif);
	uint32 w, h;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

        u_long totdata = nstrips * stripsize;
        /*
         * Read the image into memory.
         */
        tstrip_t strip;
        u_char* data = new u_char[totdata];
        u_long off = 0;
        for (strip = 0; strip < nstrips; strip++) {
            off += TIFFReadRawStrip(tif, strip, data+off, stripsize);
        }
	printf("nstrips: %d, totdata: %d, off: %d\n", nstrips, totdata, off);


	/*
	 * Here's just a simple sample on how to write the data to JPEG without lcms.
	 */ 
	FILE* outfile = fopen("test.jpeg", "wb");
	if (outfile) {
		struct jpeg_compress_struct cinfo;
		struct jpeg_error_mgr       jerr;
 
		cinfo.err = jpeg_std_error(&jerr);
		jpeg_create_compress(&cinfo);
		jpeg_stdio_dest(&cinfo, outfile);
 
		cinfo.image_width      = w;
		cinfo.image_height     = h;
		cinfo.input_components = 3;
		cinfo.in_color_space   = JCS_RGB;

		jpeg_set_defaults(&cinfo);
		/*set the quality [0..100]  */
		jpeg_set_quality (&cinfo, 75, true);
		jpeg_start_compress(&cinfo, true);

		JSAMPROW row_pointer;          /* pointer to a single row */
 
		while (cinfo.next_scanline < cinfo.image_height) {
			row_pointer = (JSAMPROW) &data[cinfo.next_scanline*3*w];
			jpeg_write_scanlines(&cinfo, &row_pointer, 1);
		}
		jpeg_finish_compress(&cinfo);

		fclose(outfile);
	}

	out = fopen("itu2.jpg", "wb");
	convertRawRGBtoITULAB(data, off, w, h, out, kk, 256);
	TIFFClose(tif); fclose(out);

	printf("ramps.tif ==> itu2.jpg Status='%s'\n", kk);

	// Back to sRGB
	in = fopen("itu2.jpg", "rb");
	out = fopen("srgb.jpg", "wb");   
	convertJPEGfromITULAB(in, out, kk, 256 );
	fclose(in); fclose(out);

	printf("itu2.jpg ==> srgb.jpg Status='%s'\n", kk);

	printf("Done!\n");
	return 0;
}
#endif

#endif
