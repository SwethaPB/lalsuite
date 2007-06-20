/*
 *  Copyright (C) 2005 Badri Krishnan, Alicia Sintes  
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the 
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *  MA  02111-1307  USA
 */

/**
 * \file DriveHough_v3.c
 * \author Badri Krishnan, Alicia Sintes 
 * \brief Driver code for performing Hough transform search on non-demodulated
   data using SFTs from possible multiple IFOs

   Revision: $Id$
 
   History:   Created by Sintes and Krishnan July 04, 2003
              Modifications for S4 January 2006

   \par Description
   
   This is the main driver for the Hough transform routines. It takes as input 
   a set of SFTs from possibly more than one IFO and outputs the number counts 
   using the Hough transform.  For a single IFO, this should be essentially equivalent 
   to DriveHough_v3.  

   \par User input

   The user inputs the following parameters:

   - Search frequency range

   - A file containing list of skypatches to search over.  For each skypatch, 
      the information is:
      - RA and dec of skypatch center.
      - Size in RA and dec.

   - Location of Directory containing the SFTs (must be v2 SFTs).

   - Interferometer (optional)

   - List of linefiles containing information about known spectral disturbanclves

   - Location of output directory and basename of output files.

   - Block size of running median for estimating psd.

   - The parameter nfSizeCylinder which determines the range of spindown parameters
      to be searched over.

   - Boolean variable for deciding if the SFTs should be inverse noise weighed.

   - Boolean variable for deciding whether amplitude modulation weights should be used.

   - Boolean variables for deciding whether the Hough maps, the statistics, list of 
      events above a threshold, and logfile should be written

   /par Output

   The output is written in several sub-directories of the specified output directory.  The
   first two items are default while the rest are written according to the user input:

   - A directory called logfiles records the user input, contents of the skypatch file 
      and cvs tags contained in the executable (if the user has required logging)

   - A directory called nstarfiles containing the loudest event for each search frequency 
      bin maximised over all sky-locations and spindown parameters.  An event is said to be
      the loudest if it has the maximum significance defined as: (number count - mean)/sigma.

   - A directory for each skypatch containing the number count statistics, the histogram, 
      the list of events, and the Hough maps 
*/


#include "./DriveHoughColor.h"
/* lalapps includes */
#include <lalapps.h>
#include <DopplerScan.h>
#include <gsl/gsl_permutation.h>

RCSID( "$Id$");



/* globals, constants and defaults */


extern int lalDebugLevel;

/* boolean global variables for controlling output */
BOOLEAN uvar_printEvents, uvar_printMaps, uvar_printStats, uvar_printSigma;

/* #define EARTHEPHEMERIS "./earth05-09.dat" */
/* #define SUNEPHEMERIS "./sun05-09.dat"    */

#define EARTHEPHEMERIS "/home/badkri/lscsoft/share/lal/earth05-09.dat"
#define SUNEPHEMERIS "/home/badkri/lscsoft/share/lal/sun05-09.dat"

#define MAXFILENAMELENGTH 512 /* maximum # of characters  of a filename */

#define DIROUT "./outMulti"   /* output directory */
#define BASENAMEOUT "HM"    /* prefix file output */

#define THRESHOLD 1.6 /* thresold for peak selection, with respect to the
                              the averaged power in the search band */
#define HOUGHTHRESHOLD 5.0 /* Hough threshold for candidate selection -- number of sigmas away from mean*/
#define SKYFILE "./skypatchfile"      
#define F0 310.0   /*  frequency to build the LUT and start search */
#define FBAND 0.05   /* search frequency band  */
#define NFSIZE  21   /* n-freq. span of the cylinder, to account for spin-down search */
#define BLOCKSRNGMED 101 /* Running median window size */

#define SKYREGION "allsky" /**< default sky region to search over -- just a single point*/

#define TRUE (1==1)
#define FALSE (1==0)

/* local function prototype */
void PrintLogFile (LALStatus *status, CHAR *dir, CHAR *basename, CHAR *skyfile, LALStringVector *linefiles, CHAR *executable );

int PrintHistogram(UINT8Vector *hist, CHAR *fnameOut, REAL8 minSignificance, REAL8 maxSignificance);

void PrintnStarFile (LALStatus *status, HoughSignificantEventVector *eventVec, CHAR *dirname, CHAR *basename);

void PrintHoughEvents (LALStatus *status, FILE *fpEvents, REAL8 houghThreshold, HOUGHMapTotal *ht, HOUGHPatchGrid *patch, HOUGHDemodPar *parDem, REAL8 mean, REAL8 sigma);

int PrintHmap2m_file(HOUGHMapTotal *ht, CHAR *fnameOut, INT4 iHmap);

int PrintHmap2file(HOUGHMapTotal *ht, CHAR *fnameOut, INT4 iHmap);

void ReadTimeStampsFile (LALStatus *status, LIGOTimeGPSVector *ts, CHAR *filename);

void LALHoughHistogramSignificance(LALStatus *status, UINT8Vector *out, HOUGHMapTotal *in, 
				   REAL8 mean, REAL8 sigma, REAL8 minSignificance, REAL8 maxSignificance);

void GetSFTVelTime(LALStatus *status, REAL8Cart3CoorVector *velV, LIGOTimeGPSVector *timeV, MultiDetectorStateSeries *in);

void GetSFTNoiseWeights(LALStatus *status, REAL8Vector *out, MultiNoiseWeights  *in);

void GetPeakGramFromMultSFTVector(LALStatus *status, HOUGHPeakGramVector *out, MultiSFTVector *in, REAL8 thr);

void SetUpSkyPatches(LALStatus *status, HoughSkyPatchesInfo *out, CHAR *skyFileName, CHAR *skyRegion, REAL8 dAlpha, REAL8 dDelta);

void GetAMWeights(LALStatus *status, REAL8Vector *out, MultiDetectorStateSeries *mdetStates, REAL8 alpha, REAL8 delta);

void SelectBestStuff(LALStatus *status, BestVariables *out, BestVariables  *in,	UINT4 mObsCohBest);

void DuplicateBestStuff(LALStatus *status, BestVariables *out, BestVariables *in);


/******************************************/

int main(int argc, char *argv[]){

  /* LALStatus pointer */
  static LALStatus  status;  
  
  /* time and velocity  */
  static LIGOTimeGPSVector    timeV;
  static REAL8Cart3CoorVector velV;
  static REAL8Vector          timeDiffV;
  LIGOTimeGPS firstTimeStamp, lastTimeStamp;
  REAL8 tObs;

  /* standard pulsar sft types */ 
  MultiSFTVector *inputSFTs = NULL;
  UINT4 binsSFT;
  UINT4 numSearchBins;
  
  /* information about all the ifos */
  MultiDetectorStateSeries *mdetStates = NULL;
  UINT4 numifo;

  /* vector of weights */
  REAL8Vector weightsV, weightsNoise;

  /* ephemeris */
  EphemerisData    *edat=NULL;

  /* struct containing subset of weights, timediff, velocity and peakgrams */
  static BestVariables best;

  /* hough structures */
  static HOUGHptfLUTVector   lutV; /* the Look Up Table vector*/
  static HOUGHPeakGramVector pgV;  /* vector of peakgrams */
  static PHMDVectorSequence  phmdVS;  /* the partial Hough map derivatives */
  static UINT8FrequencyIndexVector freqInd; /* for trajectory in time-freq plane */
  static HOUGHResolutionPar parRes;   /* patch grid information */
  static HOUGHPatchGrid  patch;   /* Patch description */ 
  static HOUGHParamPLUT  parLut;  /* parameters needed to build lut  */
  static HOUGHDemodPar   parDem;  /* demodulation parameters or  */
  static HOUGHSizePar    parSize; 
  static HOUGHMapTotal   ht;   /* the total Hough map */
  static UINT8Vector     hist; /* histogram of number counts for a single map */
  static UINT8Vector     histTotal; /* number count histogram for all maps */
  static HoughStats      stats;  /* statistical information about a Hough map */

  /* skypatch info */
  REAL8  *skyAlpha=NULL, *skyDelta=NULL, *skySizeAlpha=NULL, *skySizeDelta=NULL; 
  INT4   nSkyPatches, skyCounter=0; 
  static HoughSkyPatchesInfo skyInfo;

  /* output filenames and filepointers */
  CHAR   filehisto[ MAXFILENAMELENGTH ]; 
  CHAR   filestats[ MAXFILENAMELENGTH ]; 
  CHAR   fileEvents[ MAXFILENAMELENGTH ];
  CHAR   fileMaps[ MAXFILENAMELENGTH ];
  CHAR   fileSigma[ MAXFILENAMELENGTH ];
  FILE   *fpEvents = NULL;
  FILE   *fp1 = NULL;
  FILE   *fpSigma = NULL;

  /* the maximum number count */
  static HoughSignificantEventVector nStarEventVec;

  /* miscellaneous */
  INT4   iHmap, nSpin1Max;
  UINT4  mObsCoh, mObsCohBest;
  INT8   f0Bin, fLastBin, fBin;
  REAL8  alpha, delta, timeBase, deltaF, f1jump;
  REAL8  patchSizeX, patchSizeY;
  REAL8  alphaPeak, minSignificance, maxSignificance;
  UINT2  xSide, ySide;
  UINT2  maxNBins, maxNBorders;

  /* sft constraint variables */
  LIGOTimeGPS startTimeGPS, endTimeGPS;
  LIGOTimeGPSVector inputTimeStampsVector;

  /* user input variables */
  BOOLEAN  uvar_help, uvar_weighAM, uvar_weighNoise, uvar_printLog, uvar_printWeights;
  INT4     uvar_blocksRngMed, uvar_nfSizeCylinder, uvar_maxBinsClean, uvar_binsHisto;  
  INT4     uvar_keepBestSFTs=1;
  REAL8    uvar_startTime, uvar_endTime;
  REAL8    uvar_pixelFactor;
  REAL8    uvar_f0, uvar_peakThreshold, uvar_houghThreshold, uvar_fSearchBand;
  REAL8    uvar_dAlpha, uvar_dDelta; /* resolution for isotropic sky-grid */
  CHAR     *uvar_earthEphemeris=NULL;
  CHAR     *uvar_sunEphemeris=NULL;
  CHAR     *uvar_sftDir=NULL;
  CHAR     *uvar_dirnameOut=NULL;
  CHAR     *uvar_fbasenameOut=NULL;
  CHAR     *uvar_skyfile=NULL;
  CHAR     *uvar_timeStampsFile=NULL;
  CHAR     *uvar_skyRegion=NULL;
  LALStringVector *uvar_linefiles=NULL;


  /* Set up the default parameters */

  /* LAL error-handler */
  lal_errhandler = LAL_ERR_EXIT;
  
  lalDebugLevel = 0;  /* LALDebugLevel must be called before anything else */
  LAL_CALL( LALGetDebugLevel( &status, argc, argv, 'd'), &status);
  
  uvar_help = FALSE;
  uvar_weighAM = TRUE;
  uvar_weighNoise = TRUE;
  uvar_printLog = FALSE;
  uvar_blocksRngMed = BLOCKSRNGMED;
  uvar_nfSizeCylinder = NFSIZE;
  uvar_f0 = F0;
  uvar_fSearchBand = FBAND;
  uvar_peakThreshold = THRESHOLD;
  uvar_houghThreshold = HOUGHTHRESHOLD;
  uvar_printEvents = FALSE;
  uvar_printMaps = FALSE;
  uvar_printStats = FALSE;
  uvar_printSigma = FALSE;
  uvar_maxBinsClean = 100;
  uvar_printWeights = FALSE;
  uvar_binsHisto = 1000;
  uvar_pixelFactor = PIXELFACTOR;
  uvar_dAlpha = 0.2;
  uvar_dDelta = 0.2;

  uvar_earthEphemeris = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_earthEphemeris,EARTHEPHEMERIS);

  uvar_sunEphemeris = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_sunEphemeris,SUNEPHEMERIS);

  uvar_dirnameOut = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_dirnameOut,DIROUT);

  uvar_fbasenameOut = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_fbasenameOut,BASENAMEOUT);

  uvar_skyfile = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_skyfile,SKYFILE);

  /* register user input variables */
  LAL_CALL( LALRegisterBOOLUserVar( &status, "help",             'h',  UVAR_HELP,     "Print this message", &uvar_help), &status);  
  LAL_CALL( LALRegisterREALUserVar( &status, "f0",               'f',  UVAR_OPTIONAL, "Start search frequency", &uvar_f0), &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "fSearchBand",      'b',  UVAR_OPTIONAL, "Search frequency band", &uvar_fSearchBand), &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "startTime",          0,  UVAR_OPTIONAL, "GPS start time of observation", &uvar_startTime),        &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "endTime",          0,  UVAR_OPTIONAL, "GPS end time of observation", &uvar_endTime), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "timeStampsFile",   0,  UVAR_OPTIONAL, "Input time-stamps file", &uvar_timeStampsFile),   &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "skyRegion",        0,  UVAR_OPTIONAL, "sky-region polygon (or 'allsky')", &uvar_skyRegion), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "dAlpha",           0,  UVAR_OPTIONAL, "Resolution for flat or isotropic coarse grid (rad)", &uvar_dAlpha), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "dDelta",           0,  UVAR_OPTIONAL, "Resolution for flat or isotropic coarse grid (rad)", &uvar_dDelta), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "skyfile",          0,  UVAR_OPTIONAL, "Alternative: input skypatch file", &uvar_skyfile),         &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "peakThreshold",    0,  UVAR_OPTIONAL, "Peak selection threshold", &uvar_peakThreshold),   &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "weighAM",          0,  UVAR_OPTIONAL, "Use amplitude modulation weights", &uvar_weighAM),         &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "weighNoise",       0,  UVAR_OPTIONAL, "Use SFT noise weights", &uvar_weighNoise), &status);  
  LAL_CALL( LALRegisterINTUserVar(    &status, "keepBestSFTs",     0,  UVAR_OPTIONAL, "Number of best SFTs to use (default--keep all)", &uvar_keepBestSFTs),  &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printLog",         0,  UVAR_OPTIONAL, "Print Log file", &uvar_printLog), &status);  
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "earthEphemeris",  'E', UVAR_OPTIONAL, "Earth Ephemeris file",  &uvar_earthEphemeris),  &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "sunEphemeris",    'S', UVAR_OPTIONAL, "Sun Ephemeris file", &uvar_sunEphemeris), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "sftDir",          'D', UVAR_REQUIRED, "SFT filename pattern", &uvar_sftDir), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "dirnameOut",      'o', UVAR_OPTIONAL, "Output directory", &uvar_dirnameOut), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "fbasenameOut",     0,  UVAR_OPTIONAL, "Output file basename", &uvar_fbasenameOut), &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printMaps",        0,  UVAR_OPTIONAL, "Print Hough maps", &uvar_printMaps), &status);  
  LAL_CALL( LALRegisterREALUserVar(   &status, "houghThreshold",   0,  UVAR_OPTIONAL, "Hough threshold (No. of sigmas)", &uvar_houghThreshold),  &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printEvents",      0,  UVAR_OPTIONAL, "Print events above threshold", &uvar_printEvents),     &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printStats",       0,  UVAR_OPTIONAL, "Print Hough statistics", &uvar_printStats), &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printSigma",       0,  UVAR_OPTIONAL, "Print expected number count stdev.", &uvar_printSigma),      &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "binsHisto",        0,  UVAR_OPTIONAL, "No. of bins for histogram", &uvar_binsHisto),  &status);
  LAL_CALL( LALRegisterLISTUserVar(   &status, "linefiles",        0,  UVAR_OPTIONAL, "Comma separated List of linefiles (filenames must contain IFO name)", &uvar_linefiles), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "nfSizeCylinder",   0,  UVAR_OPTIONAL, "Size of cylinder of PHMDs", &uvar_nfSizeCylinder),  &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "pixelFactor",     'p', UVAR_OPTIONAL, "sky resolution=1/v*pixelFactor*f*Tcoh", &uvar_pixelFactor), &status);

  /* developer input variables */
  LAL_CALL( LALRegisterINTUserVar(    &status, "blocksRngMed",     0, UVAR_DEVELOPER, "Running Median block size", &uvar_blocksRngMed), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "maxBinsClean",     0, UVAR_DEVELOPER, "Maximum number of bins in cleaning", &uvar_maxBinsClean), &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printWeights",     0, UVAR_DEVELOPER, "Print relative noise weights of ifos", &uvar_printWeights), &status);  

  /* read all command line variables */
  LAL_CALL( LALUserVarReadAllInput(&status, argc, argv), &status);

  /* exit if help was required */
  if (uvar_help)
    exit(0); 

  /* very basic consistency checks on user input */
  if ( uvar_f0 < 0 ) {
    fprintf(stderr, "start frequency must be positive\n");
    exit(1);
  }

  if ( uvar_fSearchBand < 0 ) {
    fprintf(stderr, "search frequency band must be positive\n");
    exit(1);
  }
 
  if ( uvar_peakThreshold < 0 ) {
    fprintf(stderr, "peak selection threshold must be positive\n");
    exit(1);
  }

  /* probability of peak selection */
  alphaPeak = exp( -uvar_peakThreshold);


  if ( uvar_binsHisto < 1 ) {
    fprintf(stderr, "binsHisto must be at least 1\n");
    exit(1);
  }

  if ( uvar_keepBestSFTs < 1 ) {
    fprintf(stderr, "must keep at least 1 SFT\n");
    exit(1);
  }

  /* write log file with command line arguments, cvs tags, and contents of skypatch file */
  if ( uvar_printLog ) {
    LAL_CALL( PrintLogFile( &status, uvar_dirnameOut, uvar_fbasenameOut, uvar_skyfile, uvar_linefiles, argv[0]), &status);
  }


  /***** start main calculations *****/

  /* set up skypatches */
  LAL_CALL( SetUpSkyPatches( &status, &skyInfo, uvar_skyfile, uvar_skyRegion, uvar_dAlpha, uvar_dDelta), &status);
  nSkyPatches = skyInfo.numSkyPatches;
  skyAlpha = skyInfo.alpha;
  skyDelta = skyInfo.delta;
  skySizeAlpha = skyInfo.alphaSize;
  skySizeDelta = skyInfo.deltaSize;



  /* read sft Files and set up weights and nstar vector */
  {
    /* new SFT I/O data types */
    SFTCatalog *catalog = NULL;
    static SFTConstraints constraints;

    REAL8 doppWings, fmin, fmax;
    INT4 k;

    /* set detector constraint */
    constraints.detector = NULL;

    if ( LALUserVarWasSet( &uvar_startTime ) ) {
      LAL_CALL ( LALFloatToGPS( &status, &startTimeGPS, &uvar_startTime), &status);
      constraints.startTime = &startTimeGPS;
    }

    if ( LALUserVarWasSet( &uvar_endTime ) ) {
      LAL_CALL ( LALFloatToGPS( &status, &endTimeGPS, &uvar_endTime), &status);
      constraints.endTime = &endTimeGPS;
    }

    if ( LALUserVarWasSet( &uvar_timeStampsFile ) ) {
      LAL_CALL ( ReadTimeStampsFile ( &status, &inputTimeStampsVector, uvar_timeStampsFile), &status);
      constraints.timestamps = &inputTimeStampsVector;
    }
    
    /* get sft catalog */
    LAL_CALL( LALSFTdataFind( &status, &catalog, uvar_sftDir, &constraints), &status);
    if ( (catalog == NULL) || (catalog->length == 0) ) {
      fprintf (stderr,"Unable to match any SFTs with pattern '%s'\n", uvar_sftDir );
      exit(1);
    }

    /* now we can free the inputTimeStampsVector */
    if ( LALUserVarWasSet( &uvar_timeStampsFile ) ) {
      LALFree( inputTimeStampsVector.data );
    }



    /* first some sft parameters */
    deltaF = catalog->data[0].header.deltaF;  /* frequency resolution */
    timeBase= 1.0/deltaF; /* coherent integration time */
    f0Bin = floor( uvar_f0 * timeBase + 0.5); /* initial search frequency */
    numSearchBins =  uvar_fSearchBand * timeBase; /* total number of search bins - 1 */
    fLastBin = f0Bin + numSearchBins;   /* final frequency bin to be analyzed */
    

    /* read sft files making sure to add extra bins for running median */
    /* add wings for Doppler modulation and running median block size*/
    doppWings = (uvar_f0 + uvar_fSearchBand) * VTOT;    
    fmin = uvar_f0 - doppWings - (uvar_blocksRngMed + uvar_nfSizeCylinder) * deltaF;
    fmax = uvar_f0 + uvar_fSearchBand + doppWings + (uvar_blocksRngMed + uvar_nfSizeCylinder) * deltaF;

    /* read the sfts */
    LAL_CALL( LALLoadMultiSFTs ( &status, &inputSFTs, catalog, fmin, fmax), &status);
    numifo = inputSFTs->length;    

    /* find number of sfts */
    /* loop over ifos and calculate number of sfts */
    /* note that we can't use the catalog to determine the number of SFTs
       because SFTs might be segmented in frequency */
    mObsCoh = 0; /* initialization */
    for (k = 0; k < (INT4)numifo; k++ ) {
      mObsCoh += inputSFTs->data[k]->length;
    } 
    
    /* set number of SFTs to be kept */
    /* currently mobscohbest is set equal to mobscoh if no weights are used 
       -- this probably will be changed in the future */
    if ( LALUserVarWasSet( &uvar_keepBestSFTs ) && (uvar_weighNoise||uvar_weighAM)) {
      mObsCohBest = uvar_keepBestSFTs;

      /* set to mobscoh if it is more than number of sfts */
      if ( mObsCohBest > mObsCoh )
	mObsCohBest = mObsCoh;
      }  
    else 
      mObsCohBest = mObsCoh;  


    /* catalog is ordered in time so we can get start, end time and tObs*/
    firstTimeStamp = catalog->data[0].header.epoch;
    lastTimeStamp = catalog->data[catalog->length - 1].header.epoch;
    tObs = XLALGPSDiff( &lastTimeStamp, &firstTimeStamp ) + timeBase;


    /* clean sfts if required */
    if ( LALUserVarWasSet( &uvar_linefiles ) )
      {
	RandomParams *randPar=NULL;
	FILE *fpRand=NULL;
	INT4 seed, ranCount;  

	if ( (fpRand = fopen("/dev/urandom", "r")) == NULL ) {
	  fprintf(stderr,"Error in opening /dev/urandom" ); 
	  exit(1);
	} 

	if ( (ranCount = fread(&seed, sizeof(seed), 1, fpRand)) != 1 ) {
	  fprintf(stderr,"Error in getting random seed" );
	  exit(1);
	}

	LAL_CALL ( LALCreateRandomParams (&status, &randPar, seed), &status );

	LAL_CALL( LALRemoveKnownLinesInMultiSFTVector ( &status, inputSFTs, uvar_maxBinsClean, uvar_blocksRngMed, uvar_linefiles, randPar), &status);

	LAL_CALL ( LALDestroyRandomParams (&status, &randPar), &status);
	fclose(fpRand);
      } /* end cleaning */


    /* SFT info -- assume all SFTs have same length */
    binsSFT = inputSFTs->data[0]->data->data->length;

    LAL_CALL( LALDestroySFTCatalog( &status, &catalog ), &status);  	

  } /* end of sft reading block */



  /** some memory allocations */

  /* using value of length, allocate memory for most significant event nstar, fstar etc. */
  nStarEventVec.length = numSearchBins + 1;
  nStarEventVec.event = NULL;
  nStarEventVec.event = (HoughSignificantEvent *)LALCalloc( numSearchBins+1, sizeof(HoughSignificantEvent));
  /* initialize nstar significance value
     -- this should be sufficiently small so that the maximum significance is 
     always found */
  { 
    INT4 k;
    for ( k = 0; k < nStarEventVec.length; k++) {
      nStarEventVec.event[k].nStarSignificance = -sqrt( mObsCoh * alphaPeak / (1-alphaPeak));
    }
  }

  /* allocate memory for velocity vector */
  velV.length = mObsCoh;
  velV.data = NULL;
  velV.data = (REAL8Cart3Coor *)LALCalloc(1, mObsCoh*sizeof(REAL8Cart3Coor));
  
  /* allocate memory for timestamps vector */
  timeV.length = mObsCoh;
  timeV.data = NULL;
  timeV.data = (LIGOTimeGPS *)LALCalloc( 1, mObsCoh*sizeof(LIGOTimeGPS));
  
  /* allocate memory for vector of time differences from start */
  timeDiffV.length = mObsCoh;
  timeDiffV.data = NULL; 
  timeDiffV.data = (REAL8 *)LALCalloc(1, mObsCoh*sizeof(REAL8));
  
  /* allocate noise and AMweights vectors */
  weightsV.length = mObsCoh;
  weightsV.data = (REAL8 *)LALCalloc(1, mObsCoh*sizeof(REAL8));
    
  weightsNoise.length = mObsCoh;
  weightsNoise.data = (REAL8 *)LALCalloc(1, mObsCoh*sizeof(REAL8));

  /* initialize all weights to unity */
  LAL_CALL( LALHOUGHInitializeWeights( &status, &weightsNoise), &status);
  LAL_CALL( LALHOUGHInitializeWeights( &status, &weightsV), &status);
  

 
  
  /* get detector velocities weights vector, and timestamps */
  { 
    MultiNoiseWeights *multweight = NULL;    
    MultiPSDVector *multPSD = NULL;  
    INT4 tmpLeap;
    UINT4 j;
    /*     LALLeapSecFormatAndAcc lsfas = {LALLEAPSEC_GPSUTC, LALLEAPSEC_STRICT}; */
    LALLeapSecFormatAndAcc lsfas = {LALLEAPSEC_GPSUTC, LALLEAPSEC_LOOSE};

    /*  get ephemeris  */
    edat = (EphemerisData *)LALCalloc(1, sizeof(EphemerisData));
    (*edat).ephiles.earthEphemeris = uvar_earthEphemeris;
    (*edat).ephiles.sunEphemeris = uvar_sunEphemeris;
    LAL_CALL( LALLeapSecs(&status, &tmpLeap, &firstTimeStamp, &lsfas), &status);
    (*edat).leap = (INT2)tmpLeap;
    LAL_CALL( LALInitBarycenter( &status, edat), &status);


    /* normalize sfts */
    LAL_CALL( LALNormalizeMultiSFTVect (&status, &multPSD, inputSFTs, uvar_blocksRngMed), &status);
   
    /* compute multi noise weights */
    if ( uvar_weighNoise ) {
      LAL_CALL ( LALComputeMultiNoiseWeights ( &status, &multweight, multPSD, uvar_blocksRngMed, 0), &status);
    }
    
    /* we are now done with the psd */
    LAL_CALL ( LALDestroyMultiPSDVector  ( &status, &multPSD), &status);

    /* get information about all detectors including velocity and timestamps */
    /* note that this function returns the velocity at the 
       mid-time of the SFTs -- should not make any difference */
    LAL_CALL ( LALGetMultiDetectorStates ( &status, &mdetStates, inputSFTs, edat), &status);

    LAL_CALL ( GetSFTVelTime( &status, &velV, &timeV, mdetStates), &status);

    /* copy the noise-weights vector if required*/
    if ( uvar_weighNoise ) {

      LAL_CALL ( GetSFTNoiseWeights( &status, &weightsNoise, multweight), &status);

      LAL_CALL ( LALDestroyMultiNoiseWeights ( &status, &multweight), &status);
    }

    /* compute the time difference relative to startTime for all SFTs */
    for(j = 0; j < mObsCoh; j++)
      timeDiffV.data[j] = XLALGPSDiff( timeV.data + j, &firstTimeStamp );

  } /* end block for weights, velocity and time */
  

  /* print relative weights of ifos to stdout */  
  if ( uvar_printWeights )
    {
      UINT4 iIFO, iSFT, numsft, j;
      REAL8 *sumweights=NULL;
    
      sumweights = (REAL8 *)LALCalloc(1, numifo*sizeof(REAL8));
      for (j = 0, iIFO = 0; iIFO < numifo; iIFO++ ) {
	
	numsft = mdetStates->data[iIFO]->length;
	
	for ( iSFT = 0; iSFT < numsft; iSFT++, j++) 	  
	  sumweights[iIFO] += weightsNoise.data[j];	
      
      } /* loop over IFOs */
      
      for ( iIFO = 0; iIFO < numifo; iIFO++ )
	fprintf(stdout, "%d  %f\n", iIFO, sumweights[iIFO]);
      
      LALFree(sumweights);
      
    } /* end debugging */
  
     
  /* generating peakgrams  */  
  pgV.length = mObsCoh;
  pgV.pg = NULL;
  pgV.pg = (HOUGHPeakGram *)LALCalloc(mObsCoh, sizeof(HOUGHPeakGram));

  LAL_CALL( GetPeakGramFromMultSFTVector( &status, &pgV, inputSFTs, uvar_peakThreshold), &status);

  /* we are done with the sfts and ucharpeakgram now */
  LAL_CALL (LALDestroyMultiSFTVector(&status, &inputSFTs), &status );


  /* if we want to print expected sigma for each skypatch */
  if ( uvar_printSigma ) 
    {
      strcpy ( fileSigma, uvar_dirnameOut);
      strcat ( fileSigma, "/");
      strcat ( fileSigma, uvar_fbasenameOut);
      strcat ( fileSigma, "sigma");
      
      if ( (fpSigma = fopen(fileSigma,"w")) == NULL)
	{
	  fprintf(stderr,"Unable to find file %s for writing\n", fileSigma);
	  return DRIVEHOUGHCOLOR_EFILE;
	}
    } /* end if( uvar_printSigma) */


  /* min and max values significance that are possible */
  minSignificance = -sqrt(mObsCohBest * alphaPeak/(1-alphaPeak));
  maxSignificance = sqrt(mObsCohBest * (1-alphaPeak)/alphaPeak);
      

  /* loop over sky patches -- main Hough calculations */
  for (skyCounter = 0; skyCounter < nSkyPatches; skyCounter++)
    {
      UINT4 k;
      REAL8 sumWeightSquare;
      REAL8  meanN, sigmaN;
      BestVariables temp;

      /* set sky positions and skypatch sizes */
      alpha = skyAlpha[skyCounter];
      delta = skyDelta[skyCounter];
      patchSizeX = skySizeDelta[skyCounter];
      patchSizeY = skySizeAlpha[skyCounter];

      /* copy noise weights if required */
      if ( uvar_weighNoise )
	memcpy(weightsV.data, weightsNoise.data, mObsCoh * sizeof(REAL8));

      /* calculate amplitude modulation weights if required */
      if (uvar_weighAM) {
	LAL_CALL( GetAMWeights( &status, &weightsV, mdetStates, alpha, delta), &status);
      }
      
      /* sort weights vector to get the best sfts */
      temp.length = mObsCoh;
      temp.weightsV = &weightsV;
      temp.timeDiffV = &timeDiffV;
      temp.velV = &velV;
      temp.pgV = &pgV;

      if ( uvar_weighAM || uvar_weighNoise ) {
	LAL_CALL( SelectBestStuff( &status, &best, &temp, mObsCohBest), &status);	
      }
      else {
	LAL_CALL( DuplicateBestStuff( &status, &best, &temp), &status);	
      }
      
      /* calculate the sum of the weights squared */
      sumWeightSquare = 0.0;
      for ( k = 0; k < mObsCohBest; k++)
	sumWeightSquare += best.weightsV->data[k] * best.weightsV->data[k];

      /* probability of selecting a peak expected mean and standard deviation for noise only */
      meanN = mObsCohBest * alphaPeak; 
      sigmaN = sqrt(sumWeightSquare * alphaPeak * (1.0 - alphaPeak));

      if ( uvar_printSigma )
	fprintf(fpSigma, "%f \n", sigmaN);

      /* this should be  erfcInv =erfcinv(2.0 *uvar_houghFalseAlarm) */
      /* the function used is basically the inverse of the CDF for the 
	 Gaussian distribution with unit variance and
	 erfcinv(x) = gsl_cdf_ugaussian_Qinv (0.5*x)/sqrt(2) */
      /* First check that false alarm is within bounds 
	 and set it to something reasonable if not */
      /* if ( uvar_printEvents ) { */
      /* 	erfcInv = gsl_cdf_ugaussian_Qinv (uvar_houghFalseAlarm)/sqrt(2);     */
      /* 	houghThreshold = meanN + sigmaN*sqrt(2.0)*erfcInv;     */
      /*       } */
      

      /* opening the output statistics and event files */

      /*  create directory fnameout/skypatch_$j using mkdir if required */
      if ( uvar_printStats || uvar_printEvents || uvar_printMaps )
	{

	  /* create the directory name uvar_dirnameOut/skypatch_$j */
	  strcpy(  filestats, uvar_dirnameOut);
	  strcat( filestats, "/skypatch_");
	  {
	    CHAR tempstr[16];
	    sprintf(tempstr, "%d", skyCounter+1);
	    strcat( filestats, tempstr);
	  }
	  strcat( filestats, "/");
	  
	  errno = 0;
	  {
	    /* check whether file can be created or if it exists already 
	       if not then exit */
	    INT4 mkdir_result;
	    mkdir_result = mkdir(filestats, S_IRWXU | S_IRWXG | S_IRWXO);
	    if ( (mkdir_result == -1) && (errno != EEXIST) )
	      {
		fprintf(stderr, "unable to create skypatch directory %d\n", skyCounter);
		return 1;  /* stop the program */
	      }
	  } /* created directory */
	  
	  /* create the base filenames for the stats, histo and event files and template files*/
	  strcat( filestats, uvar_fbasenameOut);
	  strcpy( filehisto, filestats);

	}  /* if ( uvar_printStats || uvar_printEvents || uvar_printMaps ) */

      if ( uvar_printEvents )
	strcpy( fileEvents, filestats);

      if ( uvar_printMaps )
	strcpy(fileMaps, filestats);

      /* create and open the stats file for writing */
      if ( uvar_printStats )
	{
	  strcat(  filestats, "stats");
	  if ( (fp1 = fopen(filestats,"w")) == NULL)
	    {
	      fprintf(stderr,"Unable to find file %s for writing\n", filestats);
	      return DRIVEHOUGHCOLOR_EFILE;
	    }
	  /*setlinebuf(fp1);*/ /*line buffered on */  
	  setvbuf(fp1, (char *)NULL, _IOLBF, 0);      
	}

      if ( uvar_printEvents )
	{
	  /* create and open the events list file */
	  strcat(  fileEvents, "events");
	  if ( (fpEvents=fopen(fileEvents,"w")) == NULL ) 
	    {
	      fprintf(stderr,"Unable to find file %s\n", fileEvents);
	      return DRIVEHOUGHCOLOR_EFILE;
	    }
	  setvbuf(fpEvents, (char *)NULL, _IOLBF, 0);      
	  /*setlinebuf(fpEvents);*/ /*line buffered on */  
	}


      /****  general parameter settings and 1st memory allocation ****/      
      lutV.length    = mObsCohBest;
      lutV.lut = NULL;
      lutV.lut = (HOUGHptfLUT *)LALCalloc(mObsCohBest, sizeof(HOUGHptfLUT));
      
      phmdVS.length  = mObsCohBest;
      phmdVS.nfSize  = uvar_nfSizeCylinder;
      phmdVS.deltaF  = deltaF;
      phmdVS.phmd = NULL;
      phmdVS.phmd=(HOUGHphmd *)LALCalloc(1, mObsCohBest*uvar_nfSizeCylinder*sizeof(HOUGHphmd));
      
      freqInd.deltaF = deltaF;
      freqInd.length = mObsCohBest;
      freqInd.data = NULL;
      freqInd.data =  ( UINT8 *)LALCalloc(1, mObsCohBest*sizeof(UINT8));
      

      /* for non-demodulated data (SFT input)*/
      parDem.deltaF = deltaF;
      parDem.skyPatch.alpha = alpha;
      parDem.skyPatch.delta = delta;
      parDem.timeDiff = 0.0;
      parDem.spin.length = 0;
      parDem.spin.data = NULL;
      parDem.positC.x = 0.0;
      parDem.positC.y = 0.0;
      parDem.positC.z = 0.0;
      
      /* sky-resolution parameters **/
      parRes.deltaF = deltaF;
      parRes.patchSkySizeX  = patchSizeX;
      parRes.patchSkySizeY  = patchSizeY;
      parRes.pixelFactor = uvar_pixelFactor;
      parRes.pixErr = PIXERR;
      parRes.linErr = LINERR;
      parRes.vTotC = VTOT;

      /* allocating histogram of the number-counts in the Hough maps */
      if ( uvar_printStats ) {

	hist.length = histTotal.length = uvar_binsHisto;
	hist.data = NULL;
	histTotal.data = NULL;
	hist.data = (UINT8 *)LALCalloc( hist.length, sizeof(UINT8));
	histTotal.data = (UINT8 *)LALCalloc( histTotal.length, sizeof(UINT8));
	{ 
	  UINT4   j;
	  for(j=0; j<histTotal.length; ++j)
	    histTotal.data[j]=0;
	}
      }

      
      fBin= f0Bin;
      iHmap = 0;
      
      /* ***** for spin-down case ****/
      nSpin1Max = floor(uvar_nfSizeCylinder/2.0);
      f1jump = 1./tObs;
      
      /* start of main loop over search frequency bins */
      /********** starting the search from f0Bin to fLastBin.
		  Note one set LUT might not cover all the interval.
		  This is taken into account *******************/

      while( fBin <= fLastBin){
	INT8 fBinSearch, fBinSearchMax;
	UINT4 i,j; 
	REAL8UnitPolarCoor sourceLocation;
	
	
	parRes.f0Bin =  fBin;      
	LAL_CALL( LALHOUGHComputeNDSizePar( &status, &parSize, &parRes ),  &status );
	xSide = parSize.xSide;
	ySide = parSize.ySide;
	maxNBins = parSize.maxNBins;
	maxNBorders = parSize.maxNBorders;
	
	/* *******************create patch grid at fBin ****************  */
	patch.xSide = xSide;
	patch.ySide = ySide;
	patch.xCoor = NULL;
	patch.yCoor = NULL;
	patch.xCoor = (REAL8 *)LALCalloc(xSide, sizeof(REAL8));
	patch.yCoor = (REAL8 *)LALCalloc(ySide, sizeof(REAL8));
	LAL_CALL( LALHOUGHFillPatchGrid( &status, &patch, &parSize ), &status );
	
	/*************** other memory allocation and settings************ */
	for(j=0; j<lutV.length; ++j){
	  lutV.lut[j].maxNBins = maxNBins;
	  lutV.lut[j].maxNBorders = maxNBorders;
	  lutV.lut[j].border =
	    (HOUGHBorder *)LALCalloc(maxNBorders, sizeof(HOUGHBorder));
	  lutV.lut[j].bin =
	    (HOUGHBin2Border *)LALCalloc(maxNBins, sizeof(HOUGHBin2Border));
	  for (i=0; i<maxNBorders; ++i){
	    lutV.lut[j].border[i].ySide = ySide;
	    lutV.lut[j].border[i].xPixel =
	      (COORType *)LALCalloc(ySide, sizeof(COORType));
	  }
	}
	for(j=0; j<phmdVS.length * phmdVS.nfSize; ++j){
	  phmdVS.phmd[j].maxNBorders = maxNBorders;
	  phmdVS.phmd[j].leftBorderP =
	    (HOUGHBorder **)LALCalloc(maxNBorders, sizeof(HOUGHBorder *));
	  phmdVS.phmd[j].rightBorderP =
	    (HOUGHBorder **)LALCalloc(maxNBorders, sizeof(HOUGHBorder *));
	  phmdVS.phmd[j].ySide = ySide;
	  phmdVS.phmd[j].firstColumn = NULL;
	  phmdVS.phmd[j].firstColumn = (UCHAR *)LALCalloc(ySide, sizeof(UCHAR));
	}
	
	/* ************* create all the LUTs at fBin ********************  */  
	for (j = 0; j < mObsCohBest; ++j){  /* create all the LUTs */
	  parDem.veloC.x = best.velV->data[j].x;
	  parDem.veloC.y = best.velV->data[j].y;
	  parDem.veloC.z = best.velV->data[j].z;      
	  /* calculate parameters needed for buiding the LUT */
	  LAL_CALL( LALNDHOUGHParamPLUT( &status, &parLut, &parSize, &parDem),&status );
	  /* build the LUT */
	  LAL_CALL( LALHOUGHConstructPLUT( &status, &(lutV.lut[j]), &patch, &parLut ),
	       &status );
	}
        
	/************* build the set of  PHMD centered around fBin***********/     
	phmdVS.fBinMin = fBin - floor( uvar_nfSizeCylinder/2. );
	LAL_CALL( LALHOUGHConstructSpacePHMD(&status, &phmdVS, best.pgV, &lutV), &status );
	if (uvar_weighAM || uvar_weighNoise) {
	  LAL_CALL( LALHOUGHWeighSpacePHMD(&status, &phmdVS, best.weightsV), &status);
	}
	
	/* ************ initializing the Total Hough map space *********** */   
	ht.xSide = xSide;
	ht.ySide = ySide;
	ht.mObsCoh = mObsCohBest;
	ht.deltaF = deltaF;
	ht.map   = NULL;
	ht.map   = (HoughTT *)LALCalloc(xSide*ySide, sizeof(HoughTT));
	LAL_CALL( LALHOUGHInitializeHT( &status, &ht, &patch), &status); /*not needed */
	

	/*  Search frequency interval possible using the same LUTs */
	fBinSearch = fBin;
	fBinSearchMax = fBin + parSize.nFreqValid - 1 - floor( (uvar_nfSizeCylinder - 1)/2.0);
	

	/* Study all possible frequencies with one set of LUT */	

	while ( (fBinSearch <= fLastBin) && (fBinSearch < fBinSearchMax) ) 
	  {
	    
	    /**** study 1 spin-down. at  fBinSearch ****/

	    INT4   n;
	    REAL8  f1dis;
	    REAL8 significance;

	    ht.f0Bin = fBinSearch;
	    ht.spinRes.length = 1;
	    ht.spinRes.data = NULL;
	    ht.spinRes.data = (REAL8 *)LALCalloc(ht.spinRes.length, sizeof(REAL8));
	    
	    for ( n = 0; n <= nSpin1Max; ++n) { 
	      /*loop over all spindown values */

	      f1dis = - n * f1jump;
	      ht.spinRes.data[0] =  f1dis * deltaF;

	      /* construct path in time-freq plane */	      
	      for (j = 0 ; j < mObsCohBest; ++j){
		freqInd.data[j] = fBinSearch + floor(best.timeDiffV->data[j]*f1dis + 0.5);
	      }
	      
	      if (uvar_weighAM || uvar_weighNoise) {
		LAL_CALL( LALHOUGHConstructHMT_W( &status, &ht, &freqInd, &phmdVS ), &status );
	      }
	      else {
		LAL_CALL( LALHOUGHConstructHMT( &status, &ht, &freqInd, &phmdVS ), &status );	  
	      }

	      /* ********************* perfom stat. analysis on the maps ****************** */
	      LAL_CALL( LALHoughStatistics ( &status, &stats, &ht), &status );
	      LAL_CALL( LALStereo2SkyLocation (&status, &sourceLocation, 
				       stats.maxIndex[0], stats.maxIndex[1], &patch, &parDem), &status);

	      if ( uvar_printStats ) {

		/*LAL_CALL( LALHoughHistogram ( &status, &hist, &ht), &status);*/
		LAL_CALL( LALHoughHistogramSignificance ( &status, &hist, &ht, meanN, sigmaN, 
							  minSignificance, maxSignificance), &status);

		for(j = 0; j < histTotal.length; j++){ 
		  histTotal.data[j] += hist.data[j]; 
		}	      
	      }

	      significance =  (stats.maxCount - meanN)/sigmaN;	      
	      if ( significance > nStarEventVec.event[fBinSearch-f0Bin].nStarSignificance )
		{
		  nStarEventVec.event[fBinSearch-f0Bin].nStar = stats.maxCount;
		  nStarEventVec.event[fBinSearch-f0Bin].nStarSignificance = significance;
		  nStarEventVec.event[fBinSearch-f0Bin].freqStar = fBinSearch * deltaF;
		  nStarEventVec.event[fBinSearch-f0Bin].alphaStar = sourceLocation.alpha;
		  nStarEventVec.event[fBinSearch-f0Bin].deltaStar = sourceLocation.delta;
		  nStarEventVec.event[fBinSearch-f0Bin].fdotStar = ht.spinRes.data[0];
		}


	      /* ***** print results *********************** */

	      if( uvar_printMaps )
		  if( PrintHmap2m_file( &ht, fileMaps, iHmap ) ) return 5;

	      if ( uvar_printStats )
		fprintf(fp1, "%d %f %f %f %f %f %f %f %g\n",
			iHmap, sourceLocation.alpha, sourceLocation.delta,
			(REAL4)stats.maxCount, (REAL4)stats.minCount, stats.avgCount,stats.stdDev,
			(fBinSearch*deltaF), ht.spinRes.data[0]);

	      if ( uvar_printEvents )
		LAL_CALL( PrintHoughEvents (&status, fpEvents, uvar_houghThreshold, &ht,
					    &patch, &parDem, meanN, sigmaN), &status );

	      ++iHmap;
	    } /* end loop over spindown values */ 
	    
	    LALFree(ht.spinRes.data);
	    
	    
	    /***** shift the search freq. & PHMD structure 1 freq.bin ****** */
	    ++fBinSearch;
	    
	    LAL_CALL( LALHOUGHupdateSpacePHMDup(&status, &phmdVS, best.pgV, &lutV), &status );
	    
	    if (uvar_weighAM || uvar_weighNoise) {
	      LAL_CALL( LALHOUGHWeighSpacePHMD( &status, &phmdVS, best.weightsV), &status);	    
	    }

	  }   /* ********>>>>>>  closing second while  <<<<<<<<**********<  */
	
	fBin = fBinSearch;
	
	/* ********************  Free partial memory ******************* */
	LALFree(patch.xCoor);
	LALFree(patch.yCoor);
	LALFree(ht.map);
	
	for (j=0; j<lutV.length ; ++j){
	  for (i=0; i<maxNBorders; ++i){
	    LALFree( lutV.lut[j].border[i].xPixel);
	  }
	  LALFree( lutV.lut[j].border);
	  LALFree( lutV.lut[j].bin);
	}
	for(j=0; j<phmdVS.length * phmdVS.nfSize; ++j){
	  LALFree( phmdVS.phmd[j].leftBorderP);
	  LALFree( phmdVS.phmd[j].rightBorderP);
	  LALFree( phmdVS.phmd[j].firstColumn);
	}
	
      } /* closing while */
      
      /******************************************************************/
      /* printing total histogram */
      /******************************************************************/
      if ( uvar_printStats ) 
	{
	  if( PrintHistogram( &histTotal, filehisto, minSignificance, maxSignificance) ) return 7;
	}

      /******************************************************************/
      /* closing files with statistics results and events */
      /******************************************************************/  
      if ( uvar_printStats )
	fclose(fp1);

      if ( uvar_printEvents )
	fclose(fpEvents);


     

      /******************************************************************/
      /* Free memory allocated inside skypatches loop */
      /******************************************************************/
      
      LALFree(lutV.lut);  
      
      LALFree(phmdVS.phmd);
      LALFree(freqInd.data);

      if ( uvar_printStats ) {
	LALFree(hist.data);
	LALFree(histTotal.data);
      }

    } /* finish loop over skypatches */

  /* close sigma file */
  if ( uvar_printSigma )
    fclose(fpSigma);

  /* print most significant events */
  LAL_CALL( PrintnStarFile( &status, &nStarEventVec, uvar_dirnameOut, 
			    uvar_fbasenameOut), &status);


  {
    UINT4 j;
    for (j = 0; j < mObsCoh; ++j) LALFree( pgV.pg[j].peak); 
  }
  LALFree(pgV.pg);
  
  LALFree(timeV.data);
  LALFree(timeDiffV.data);
  

  LALFree(velV.data);

  LALFree(weightsV.data);
  weightsV.data = NULL;

  LALFree(weightsNoise.data);  

  XLALDestroyMultiDetectorStateSeries ( mdetStates );

  LALFree(edat->ephemE);
  LALFree(edat->ephemS);
  LALFree(edat);

  LALFree(skyAlpha);
  LALFree(skyDelta);
  LALFree(skySizeAlpha);
  LALFree(skySizeDelta);

  LALFree(nStarEventVec.event);



  LALFree(best.weightsV->data);
  
  LALFree(best.weightsV);
  

  
  LALFree(best.timeDiffV->data);
  LALFree(best.timeDiffV);
  LALFree(best.velV->data);
  LALFree(best.velV);
  LALFree(best.pgV->pg);
  LALFree(best.pgV);


  LAL_CALL (LALDestroyUserVars(&status), &status);

  LALCheckMemoryLeaks();

  if ( lalDebugLevel )
    REPORTSTATUS ( &status);

  return status.statusCode;
}



  
/******************************************************************/
/* printing the Histogram of all maps into a file                    */
/******************************************************************/
  
int PrintHistogram(UINT8Vector *hist, CHAR *fnameOut, REAL8 minSignificance, REAL8 maxSignificance)
{

  INT4 binsHisto;
  REAL8 dSig;
  FILE  *fp=NULL;   /* Output file */
  char filename[ MAXFILENAMELENGTH ];
  INT4  i ;
  
  strcpy(  filename, fnameOut);
  strcat(  filename, "histo");

  binsHisto = hist->length;
  dSig = (maxSignificance - minSignificance)/binsHisto;
  
  if ( (fp = fopen(filename,"w")) == NULL)
    {  
      fprintf(stderr,"Unable to find file %s\n",filename);
      return DRIVEHOUGHCOLOR_EFILE; 
    }

  for (i = 0; i < binsHisto; i++){
    fprintf(fp,"%g  %llu\n", minSignificance + i*dSig, hist->data[i]);
  }
  
  fclose( fp );  
  return 0;

}



/******************************************************************/
/* printing the HM into a file                    */
/******************************************************************/

int PrintHmap2file(HOUGHMapTotal *ht, CHAR *fnameOut, INT4 iHmap){

  FILE  *fp=NULL;   /* Output file */
  char filename[ MAXFILENAMELENGTH ], filenumber[16]; 
  INT4  k, i ;
  UINT2 xSide, ySide;
   
  strcpy(  filename, fnameOut);
  sprintf( filenumber, ".%06d",iHmap); 
  strcat(  filename, filenumber);

  if ( (fp = fopen(filename,"w")) == NULL)
    {  
      fprintf(stderr,"Unable to find file %s\n",filename);
      return DRIVEHOUGHCOLOR_EFILE; 
    }

  ySide= ht->ySide;
  xSide= ht->xSide;

  for(k=ySide-1; k>=0; --k){
    for(i=0;i<xSide;++i){
      fprintf( fp ," %f", (REAL4)ht->map[k*xSide +i]);
      fflush( fp );
    }
    fprintf( fp ," \n");
    fflush( fp );
  }

  fclose( fp );  
  return 0;
}


/******************************************************************/
/* printing the HM into a m_file                    */
/******************************************************************/

int PrintHmap2m_file(HOUGHMapTotal *ht, CHAR *fnameOut, INT4 iHmap){

  FILE  *fp=NULL;   /* Output file */
  char filename[ MAXFILENAMELENGTH ], filenumber[16]; 
  INT4  k, i ;
  UINT2 xSide, ySide;
  INT4 mObsCoh;
  REAL8 f0,f1;
   
  strcpy(  filename, fnameOut);
  sprintf( filenumber, "%06d.m",iHmap); 
  strcat(  filename, filenumber);
  fp=fopen(filename,"w");

  if ( !fp ){  
    fprintf(stderr,"Unable to find file %s\n",filename);
    return DRIVEHOUGHCOLOR_EFILE; 
  }

  ySide= ht->ySide;
  xSide= ht->xSide;
  f0=ht->f0Bin* ht->deltaF;
  mObsCoh = ht->mObsCoh;
  f1=0.0;
  if( ht->spinRes.length ){ f1=ht->spinRes.data[0]; }
  
  /* printing into matlab format */
  
  fprintf( fp ,"f0= %f ; \n", f0);
  fprintf( fp ,"f1= %g ; \n", f1);
  fprintf( fp ,"map= [ \n");

  for(k=ySide-1; k>=0; --k){
    for(i=0;i<xSide;++i){
      fprintf( fp ," %f", (REAL4)ht->map[k*xSide +i]);
      fflush( fp );
    }
    fprintf( fp ," \n");
    fflush( fp );
  }
  
  fprintf( fp ,"    ]; \n");
  fclose( fp );  
  return 0;
}


/******************************************************************/
/*  Find and print events to a given open file */
/******************************************************************/
void PrintHoughEvents (LALStatus       *status,
        	      FILE            *fpEvents,
	  	      REAL8           houghThreshold,
		      HOUGHMapTotal   *ht,
	    	      HOUGHPatchGrid  *patch,
	 	      HOUGHDemodPar   *parDem,
		      REAL8           mean,
		      REAL8           sigma )
{

  REAL8UnitPolarCoor sourceLocation;
  UINT2    xPos, yPos, xSide, ySide;
  REAL8    temp;
  REAL8    f0;
  /* --------------------------------------------- */
  INITSTATUS (status, "PrintHoughEvents", rcsid);
  ATTATCHSTATUSPTR (status);
  
 /* make sure arguments are not null */
  ASSERT (patch , status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (parDem, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (ht, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL);

 /* make sure input hough map is ok*/
  ASSERT (ht->xSide > 0, status, DRIVEHOUGHCOLOR_EBAD,DRIVEHOUGHCOLOR_MSGEBAD);
  ASSERT (ht->ySide > 0, status, DRIVEHOUGHCOLOR_EBAD,DRIVEHOUGHCOLOR_MSGEBAD);
  
  /* read input parameters */
  xSide = ht->xSide;
  ySide = ht->ySide; 
  
  f0=(ht->f0Bin)*(ht->deltaF);
  
  for(yPos =0; yPos<ySide; yPos++){
    for(xPos =0; xPos<xSide; xPos++){
      /* read the current number count */
      temp = (ht->map[yPos*xSide + xPos] - mean)/sigma;
      if(temp > houghThreshold){
        TRY( LALStereo2SkyLocation(status->statusPtr, 
				&sourceLocation,xPos,yPos,patch, parDem), status);
	if (ht->spinRes.length) {
	  fprintf(fpEvents, "%f %f %f %f %g \n", 
		  temp, sourceLocation.alpha, sourceLocation.delta, 
		  f0, ht->spinRes.data[0]);
	}
	else {
	  fprintf(fpEvents, "%f %f %f %f %g \n", 
		  temp, sourceLocation.alpha, sourceLocation.delta, 
		  f0,0.00);
	}
      }      
    }
  }
  	 
  DETATCHSTATUSPTR (status);
  /* normal exit */
  RETURN (status);
}    
/* >>>>>>>>>>>>>>>>>>>>>*************************<<<<<<<<<<<<<<<<<<<< */





void PrintLogFile (LALStatus       *status,
		   CHAR            *dir,
		   CHAR            *basename,
		   CHAR            *skyfile,
		   LALStringVector *linefiles,
		   CHAR            *executable )
{
  CHAR *fnameLog=NULL; 
  FILE *fpLog=NULL;
  CHAR *logstr=NULL; 
  UINT4 k;

  INITSTATUS (status, "PrintLogFile", rcsid);
  ATTATCHSTATUSPTR (status);
  
  /* open log file for writing */
  fnameLog = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(fnameLog,dir);
  strcat(fnameLog, "/logfiles/");
  /* now create directory fdirOut/logfiles using mkdir */
  errno = 0;
  {
    /* check whether file can be created or if it exists already 
       if not then exit */
    INT4 mkdir_result;
    mkdir_result = mkdir(fnameLog, S_IRWXU | S_IRWXG | S_IRWXO);
    if ( (mkdir_result == -1) && (errno != EEXIST) )
      {
	fprintf(stderr, "unable to create logfiles directory %s\n", fnameLog);
        LALFree(fnameLog);
	exit(1);  /* stop the program */
      }
  }

  /* create the logfilename in the logdirectory */
  strcat(fnameLog, basename);
  strcat(fnameLog,".log");
  /* open the log file for writing */
  if ((fpLog = fopen(fnameLog, "w")) == NULL) {
    fprintf(stderr, "Unable to open file %s for writing\n", fnameLog);
    LALFree(fnameLog);
    exit(1);
  }
  
  /* get the log string */
  TRY( LALUserVarGetLog(status->statusPtr, &logstr, UVAR_LOGFMT_CFGFILE), status);  

  fprintf( fpLog, "## LOG FILE FOR Hough Driver\n\n");
  fprintf( fpLog, "# User Input:\n");
  fprintf( fpLog, "#-------------------------------------------\n");
  fprintf( fpLog, logstr);
  LALFree(logstr);

  /* copy contents of skypatch file into logfile */
  fprintf(fpLog, "\n\n# Contents of skypatch file:\n");
  fclose(fpLog);
  {
    CHAR command[1024] = "";
    sprintf(command, "cat %s >> %s", skyfile, fnameLog);
    system(command);
  }


  /* copy contents of linefile if necessary */
  if ( linefiles ) {

    for ( k = 0; k < linefiles->length; k++) {
      
      if ((fpLog = fopen(fnameLog, "a")) != NULL) {
	CHAR command[1024] = "";
	fprintf (fpLog, "\n\n# Contents of linefile %s :\n", linefiles->data[k]);
	fprintf (fpLog, "# -----------------------------------------\n");
	fclose (fpLog);
	sprintf(command, "cat %s >> %s", linefiles->data[k], fnameLog);      
	system (command);	 
      } 
    } 
  }

  /* append an ident-string defining the exact CVS-version of the code used */
  if ((fpLog = fopen(fnameLog, "a")) != NULL) 
    {
      CHAR command[1024] = "";
      fprintf (fpLog, "\n\n# CVS-versions of executable:\n");
      fprintf (fpLog, "# -----------------------------------------\n");
      fclose (fpLog);
      
      sprintf (command, "ident %s | sort -u >> %s", executable, fnameLog);
      system (command);	/* we don't check this. If it fails, we assume that */
    			/* one of the system-commands was not available, and */
    			/* therefore the CVS-versions will not be logged */ 
    }

  LALFree(fnameLog); 
  	 
  DETATCHSTATUSPTR (status);
  /* normal exit */
  RETURN (status);
}    


/* print most significant events */
void PrintnStarFile (LALStatus                   *status,
		     HoughSignificantEventVector *eventVec,
		     CHAR                        *dirname,
		     CHAR                        *basename)
{
  CHAR *filestar = NULL; 
  FILE *fpStar = NULL; 
  INT4 length, starIndex;
  HoughSignificantEvent *event;
  INT4 mkdir_result;

  INITSTATUS (status, "PrintnStarFile", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT(eventVec, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 
  ASSERT(eventVec->event, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 
  ASSERT(dirname, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 
  ASSERT(basename, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 

  /* create the directory for writing nstar */
  filestar = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy( filestar, dirname);
  strcat( filestar, "/nstarfiles/");
  errno = 0;
  /* check whether file can be created or if it exists already 
     if not then exit */
  mkdir_result = mkdir(filestar, S_IRWXU | S_IRWXG | S_IRWXO);
  if ( (mkdir_result == -1) && (errno != EEXIST) ) {
    ABORT( status, DRIVEHOUGHCOLOR_EDIR, DRIVEHOUGHCOLOR_MSGEDIR);
  }
  
  strcat( filestar, basename );
  strcat( filestar, "nstar");

  /* open the nstar file for writing */
  if ( (fpStar = fopen(filestar,"w")) == NULL) {
    ABORT( status, DRIVEHOUGHCOLOR_EFILE, DRIVEHOUGHCOLOR_MSGEFILE);
  }
  
  /*line buffering */  
  setvbuf(fpStar, (char *)NULL, _IOLBF, 0);      

  /* write the nstar results */
  length = eventVec->length;
  event = eventVec->event;
  for(starIndex = 0; starIndex < length; starIndex++)
    {   
      fprintf(fpStar, "%f %f %f %f %f %g \n", event->nStar, event->nStarSignificance, event->freqStar, 
	      event->alphaStar, event->deltaStar, event->fdotStar );
      event++;
    }

  /* close nstar file */
  fclose(fpStar);
  LALFree(filestar); 
  	 
  DETATCHSTATUSPTR (status);
  /* normal exit */
  RETURN (status);
}    



/* read timestamps file */
void ReadTimeStampsFile (LALStatus          *status,
			 LIGOTimeGPSVector  *ts,
			 CHAR               *filename)
{

  FILE  *fp = NULL;
  INT4  numTimeStamps, r;
  UINT4 j;
  REAL8 temp1, temp2;

  INITSTATUS (status, "ReadTimeStampsFile", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT(ts, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 
  ASSERT(ts->data == NULL, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 
  ASSERT(ts->length == 0, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 
  ASSERT(filename, status, DRIVEHOUGHCOLOR_ENULL,DRIVEHOUGHCOLOR_MSGENULL); 

  if ( (fp = fopen(filename, "r")) == NULL) {
    ABORT( status, DRIVEHOUGHCOLOR_EFILE, DRIVEHOUGHCOLOR_MSGEFILE);
  }

  /* count number of timestamps */
  numTimeStamps = 0;     

  do {
    r = fscanf(fp,"%lf%lf\n", &temp1, &temp2);
    /* make sure the line has the right number of entries or is EOF */
    if (r==2) numTimeStamps++;
  } while ( r != EOF);
  rewind(fp);

  ts->length = numTimeStamps;
  ts->data = LALCalloc (1, numTimeStamps * sizeof(LIGOTimeGPS));;
  if ( ts->data == NULL ) {
    fclose(fp);
    ABORT( status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  }
  
  for (j = 0; j < ts->length; j++)
    {
      r = fscanf(fp,"%lf%lf\n", &temp1, &temp2);
      ts->data[j].gpsSeconds = (INT4)temp1;
      ts->data[j].gpsNanoSeconds = (INT4)temp2;
    }
  
  fclose(fp);
  	 
  DETATCHSTATUSPTR (status);
  /* normal exit */
  RETURN (status);
}    



/** given a total hough map, this function produces a histogram of
   the number count significance */
void LALHoughHistogramSignificance(LALStatus      *status,
				   UINT8Vector    *out,
				   HOUGHMapTotal  *in,
				   REAL8          mean,
				   REAL8          sigma,
				   REAL8          minSignificance,
				   REAL8          maxSignificance)
{

  INT4   i, j, binsHisto, xSide, ySide, binIndex;
  REAL8  temp;

  INITSTATUS (status, "LALHoughHistogramSignificance", rcsid);
  ATTATCHSTATUSPTR (status);

  /* make sure arguments are not null */
  ASSERT (in, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->map, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (out, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  /* make sure input hough map is ok*/
  ASSERT (in->xSide > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
  ASSERT (in->ySide > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
  ASSERT (in->mObsCoh > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  ASSERT (out->length > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  binsHisto = out->length;
  xSide = in->xSide;
  ySide = in->ySide;

  /* initialize histogram vector*/
  for (i=0; i < binsHisto; i++) 
    out->data[i] = 0;

  /* loop over hough map and find histogram */
  for (i = 0; i < ySide; i++){
    for (j = 0; j < xSide; j++){

      /* calculate significance of number count */
      temp = (in->map[i*xSide + j] - mean)/sigma;

      /* make sure temp is in proper range */
      ASSERT (temp > minSignificance, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
      ASSERT (temp < maxSignificance, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

      binIndex = (INT4) binsHisto * (temp - minSignificance)/(maxSignificance - minSignificance);

      /* make sure binIndex is in proper range */
      ASSERT (binIndex >= 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
      ASSERT (binIndex <= binsHisto-1, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

      /* add to relevant entry in histogram */
      out->data[binIndex] += 1;
    }
  }

  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);
}



void GetSFTVelTime(LALStatus                *status,
		   REAL8Cart3CoorVector     *velV,
		   LIGOTimeGPSVector        *timeV, 
		   MultiDetectorStateSeries *in)
{

  UINT4 numifo, numsft, iIFO, iSFT, j;  
  
  INITSTATUS (status, "GetSFTVelTime", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT (in, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->length > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  ASSERT (velV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (velV->length > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (velV->data, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  ASSERT (timeV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (timeV->length > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (timeV->data, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  ASSERT (velV->length == timeV->length, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  numifo = in->length;
  
  /* copy the timestamps, weights, and velocity vector */
  for (j = 0, iIFO = 0; iIFO < numifo; iIFO++ ) {

    ASSERT (in->data[iIFO], status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);    
    
    numsft = in->data[iIFO]->length;    
    ASSERT (numsft > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);    

    for ( iSFT = 0; iSFT < numsft; iSFT++, j++) {
      
      velV->data[j].x = in->data[iIFO]->data[iSFT].vDetector[0];
      velV->data[j].y = in->data[iIFO]->data[iSFT].vDetector[1];
      velV->data[j].z = in->data[iIFO]->data[iSFT].vDetector[2];
      
      /* mid time of sfts */
      timeV->data[j] = in->data[iIFO]->data[iSFT].tGPS;
      
    } /* loop over SFTs */
    
  } /* loop over IFOs */
  
  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);
}




void GetSFTNoiseWeights(LALStatus          *status,
			REAL8Vector        *out,
			MultiNoiseWeights  *in)
{

  UINT4 numifo, numsft, iIFO, iSFT, j;  
  
  INITSTATUS (status, "GetSFTNoiseWeights", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT (in, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->length > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  ASSERT (out, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (out->length > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (out->data, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  numifo = in->length;
  
  /* copy the timestamps, weights, and velocity vector */
  for (j = 0, iIFO = 0; iIFO < numifo; iIFO++ ) {

    ASSERT (in->data[iIFO], status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);    
    
    numsft = in->data[iIFO]->length;    
    ASSERT (numsft > 0, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);    

    for ( iSFT = 0; iSFT < numsft; iSFT++, j++) {

      out->data[j] = in->data[iIFO]->data[iSFT];      
      
    } /* loop over SFTs */
    
  } /* loop over IFOs */

  TRY( LALHOUGHNormalizeWeights( status->statusPtr, out), status);
  
  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);
}



/** Loop over SFTs and set a threshold to get peakgrams.  SFTs must be normalized.  */
void GetPeakGramFromMultSFTVector(LALStatus           *status,
				  HOUGHPeakGramVector *out, /**< Output peakgrams */
				  MultiSFTVector      *in,  /**< Input SFTs */
				  REAL8               thr   /**< Threshold on SFT power */)  
{

  SFTtype  *sft;
  UCHARPeakGram  pg1;
  INT4   nPeaks;
  UINT4  iIFO, iSFT, numsft, numifo, j, binsSFT; 
  
  INITSTATUS (status, "GetSFTNoiseWeights", rcsid);
  ATTATCHSTATUSPTR (status);

  numifo = in->length;
  binsSFT = in->data[0]->data->data->length;
  
  pg1.length = binsSFT;
  pg1.data = NULL;
  pg1.data = (UCHAR *)LALCalloc( 1, binsSFT*sizeof(UCHAR));
  
  /* loop over sfts and select peaks */
  for ( j = 0, iIFO = 0; iIFO < numifo; iIFO++){
    
    numsft = in->data[iIFO]->length;
    
    for ( iSFT = 0; iSFT < numsft; iSFT++, j++) {
      
      sft = in->data[iIFO]->data + iSFT;
      
      TRY (SFTtoUCHARPeakGram( status->statusPtr, &pg1, sft, thr), status);
      
      nPeaks = pg1.nPeaks;

      /* compress peakgram */      
      out->pg[j].length = nPeaks;
      out->pg[j].peak = NULL;
      out->pg[j].peak = (INT4 *)LALCalloc( 1, nPeaks*sizeof(INT4));
      
      TRY( LALUCHAR2HOUGHPeak( status->statusPtr, &(out->pg[j]), &pg1), status );
      
    } /* loop over SFTs */

  } /* loop over IFOs */

  LALFree(pg1.data);

  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);

}


/** Set up location of skypatch centers and sizes 
    If user specified skyRegion then use DopplerScan function
    to construct an isotropic grid. Otherwise use skypatch file. */
void SetUpSkyPatches(LALStatus           *status,
		     HoughSkyPatchesInfo *out,   /**< output skypatches info */
		     CHAR                *skyFileName, /**< name of skypatch file */
		     CHAR                *skyRegion,  /**< skyregion (if isotropic grid is to be constructed) */
		     REAL8               dAlpha,      /**< alpha resolution (if isotropic grid is to be constructed) */
		     REAL8               dDelta)  /**< delta resolution (if isotropic grid is to be constructed) */
{

  DopplerSkyScanInit scanInit = empty_DopplerSkyScanInit;   /* init-structure for DopperScanner */
  DopplerSkyScanState thisScan = empty_DopplerSkyScanState; /* current state of the Doppler-scan */
  UINT4 nSkyPatches, skyCounter;
  PulsarDopplerParams dopplerpos;	  
  
  INITSTATUS (status, "SetUpSkyPatches", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT (out, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (dAlpha > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
  ASSERT (dDelta > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  if (skyRegion ) {
    
    scanInit.dAlpha = dAlpha;
    scanInit.dDelta = dDelta;
    scanInit.gridType = GRID_ISOTROPIC;
    scanInit.metricType =  LAL_PMETRIC_NONE;
    /* scanInit.metricMismatch = 0; */
    /* scanInit.projectMetric = TRUE; */
    /* scanInit.obsDuration = tStack; */
    /* scanInit.obsBegin = tMidGPS; */
    /* scanInit.Detector = &(stackMultiDetStates.data[0]->data[0]->detector); */
    /* scanInit.ephemeris = edat; */
    /* scanInit.skyGridFile = uvar_skyGridFile; */
    scanInit.skyRegionString = (CHAR*)LALCalloc(1, strlen(skyRegion)+1);
    strcpy (scanInit.skyRegionString, skyRegion);
    /*   scanInit.Freq = usefulParams.spinRange_midTime.fkdot[0] +  usefulParams.spinRange_midTime.fkdotBand[0]; */
    
    /* set up the grid */
    TRY ( InitDopplerSkyScan ( status->statusPtr, &thisScan, &scanInit), status); 
    
    nSkyPatches = out->numSkyPatches = thisScan.numSkyGridPoints;
    
    out->alpha = (REAL8 *)LALCalloc(1, nSkyPatches*sizeof(REAL8));
    out->delta = (REAL8 *)LALCalloc(1, nSkyPatches*sizeof(REAL8));     
    out->alphaSize = (REAL8 *)LALCalloc(1, nSkyPatches*sizeof(REAL8));
    out->deltaSize = (REAL8 *)LALCalloc(1, nSkyPatches*sizeof(REAL8));     
        
    /* loop over skygrid points */  
    XLALNextDopplerSkyPos(&dopplerpos, &thisScan);
    
    skyCounter = 0; 
    while(thisScan.state != STATE_FINISHED) {
      
      out->alpha[skyCounter] = dopplerpos.Alpha;
      out->delta[skyCounter] = dopplerpos.Delta;
      out->alphaSize[skyCounter] = dAlpha;
      out->deltaSize[skyCounter] = dDelta;
      
      if ((dopplerpos.Delta>0) && (dopplerpos.Delta < atan(4*LAL_PI/dAlpha/dDelta) ))
        out->alphaSize[skyCounter] = dAlpha*cos(dopplerpos.Delta -0.5*dDelta)/cos(dopplerpos.Delta);

      if ((dopplerpos.Delta<0) && (dopplerpos.Delta > -atan(4*LAL_PI/dAlpha/dDelta) ))
        out->alphaSize[skyCounter] = dAlpha*cos(dopplerpos.Delta +0.5*dDelta)/cos(dopplerpos.Delta);
      
      XLALNextDopplerSkyPos(&dopplerpos, &thisScan);
      skyCounter++;
      
    } /* end while loop over skygrid */
      
    /* free dopplerscan stuff */
    TRY ( FreeDopplerSkyScan( status->statusPtr, &thisScan), status);
    if ( scanInit.skyRegionString )
      LALFree ( scanInit.skyRegionString );

  } else {

    /* read skypatch info */
    {
      FILE   *fpsky = NULL; 
      INT4   r;
      REAL8  temp1, temp2, temp3, temp4;

      ASSERT (skyFileName, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
      
      if ( (fpsky = fopen(skyFileName, "r")) == NULL)
	{
	  ABORT ( status, DRIVEHOUGHCOLOR_EFILE, DRIVEHOUGHCOLOR_MSGEFILE );
	}
      
      nSkyPatches = 0;
      do 
	{
	  r = fscanf(fpsky,"%lf%lf%lf%lf\n", &temp1, &temp2, &temp3, &temp4);
	  /* make sure the line has the right number of entries or is EOF */
	  if (r==4) nSkyPatches++;
	} while ( r != EOF);
      rewind(fpsky);

      out->numSkyPatches = nSkyPatches;      
      out->alpha = (REAL8 *)LALCalloc(nSkyPatches, sizeof(REAL8));
      out->delta = (REAL8 *)LALCalloc(nSkyPatches, sizeof(REAL8));     
      out->alphaSize = (REAL8 *)LALCalloc(nSkyPatches, sizeof(REAL8));
      out->deltaSize = (REAL8 *)LALCalloc(nSkyPatches, sizeof(REAL8));     
      
      for (skyCounter = 0; skyCounter < nSkyPatches; skyCounter++)
	{
	  r = fscanf(fpsky,"%lf%lf%lf%lf\n", out->alpha + skyCounter, out->delta + skyCounter, 
		     out->alphaSize + skyCounter,  out->deltaSize + skyCounter);
	}
      
      fclose(fpsky);     
    } /* end skypatchfile reading block */    
  } /* end setting up of skypatches */


  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);

}


void GetAMWeights(LALStatus                *status,
		  REAL8Vector              *out,
		  MultiDetectorStateSeries *mdetStates,		  
		  REAL8                    alpha,
		  REAL8                    delta)
{

  MultiAMCoeffs *multiAMcoef = NULL;
  UINT4 iIFO, iSFT, k, numsft, numifo;
  REAL8 a, b;
  SkyPosition skypos;
  
  INITSTATUS (status, "GetAMWeights", rcsid);
  ATTATCHSTATUSPTR (status);
  
  /* get the amplitude modulation coefficients */
  skypos.longitude = alpha;
  skypos.latitude = delta;
  skypos.system = COORDINATESYSTEM_EQUATORIAL;
  TRY ( LALGetMultiAMCoeffs ( status->statusPtr, &multiAMcoef, mdetStates, skypos), status);

  numifo = mdetStates->length;
  
  /* loop over the weights and multiply them by the appropriate
     AM coefficients */
  for ( k = 0, iIFO = 0; iIFO < numifo; iIFO++) {
    
    numsft = mdetStates->data[iIFO]->length;
    
    for ( iSFT = 0; iSFT < numsft; iSFT++, k++) {	  
            
      a = multiAMcoef->data[iIFO]->a->data[iSFT];
      b = multiAMcoef->data[iIFO]->b->data[iSFT];    
      out->data[k] *= (a*a + b*b);

    } /* loop over SFTs */

  } /* loop over IFOs */
  
  TRY( LALHOUGHNormalizeWeights( status->statusPtr, out), status);
  
  XLALDestroyMultiAMCoeffs ( multiAMcoef );
  

  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);

}



void SelectBestStuff(LALStatus      *status,
		     BestVariables  *out,
		     BestVariables  *in,		  
		     UINT4          mObsCohBest)
{

  UINT4 *index=NULL;
  UINT4 k, mObsCoh;

  INITSTATUS (status, "SelectBestStuff", rcsid);
  ATTATCHSTATUSPTR (status);

  /* check consistency of input */
  ASSERT (out, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  ASSERT (in, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->length > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
  mObsCoh = in->length;

  ASSERT (in->weightsV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->weightsV->length == mObsCoh, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  ASSERT (in->timeDiffV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->timeDiffV->length == mObsCoh, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  ASSERT (in->velV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->velV->length == mObsCoh, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  ASSERT (mObsCohBest > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
  ASSERT (mObsCohBest <= in->length, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  /* memory allocation for output -- use reallocs because this 
     function may be called within the loop over sky positions, so memory might
     already have been allocated previously */
  out->length = mObsCohBest;


  if (out->weightsV == NULL) {
    out->weightsV = (REAL8Vector *)LALCalloc(1, sizeof(REAL8Vector));
    out->weightsV->length = mObsCohBest;
    out->weightsV->data = (REAL8 *)LALCalloc(1, mObsCohBest*sizeof(REAL8));	
  }

  if (out->timeDiffV == NULL) {
    out->timeDiffV = (REAL8Vector *)LALCalloc(1, sizeof(REAL8Vector));
    out->timeDiffV->length = mObsCohBest;
    out->timeDiffV->data = (REAL8 *)LALCalloc(1, mObsCohBest*sizeof(REAL8));	
  }

  if (out->velV == NULL) {
    out->velV =  (REAL8Cart3CoorVector *)LALCalloc(1, sizeof(REAL8Cart3CoorVector));
    out->velV->length = mObsCohBest;
    out->velV->data = (REAL8Cart3Coor *)LALCalloc(1, mObsCohBest*sizeof(REAL8Cart3Coor));
  }

  if (out->pgV == NULL) {
    out->pgV = (HOUGHPeakGramVector *)LALCalloc(1, sizeof(HOUGHPeakGramVector));
    out->pgV->length = mObsCohBest;
    out->pgV->pg = (HOUGHPeakGram *)LALCalloc(1, mObsCoh*sizeof(HOUGHPeakGram));
  }

  index = LALCalloc(1, mObsCohBest*sizeof(UINT4));  
  gsl_sort_largest_index( index, mObsCohBest, in->weightsV->data, 1, mObsCoh);	

  for ( k = 0; k < mObsCohBest; k++) {

    out->weightsV->data[k] = in->weightsV->data[index[k]];    
    out->timeDiffV->data[k] = in->timeDiffV->data[index[k]];    

    out->velV->data[k].x = in->velV->data[index[k]].x;
    out->velV->data[k].y = in->velV->data[index[k]].y;
    out->velV->data[k].z = in->velV->data[index[k]].z;

    /* this copies the pointers to the peakgram data from the input */
    out->pgV->pg[k] = in->pgV->pg[index[k]];

  }

  /*   gsl_sort_index( index, timeDiffV.data, 1, mObsCohBest);	 */
  
  LALFree(index);

  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);

}


/* copy data in BestVariables struct */
void DuplicateBestStuff(LALStatus      *status,
			BestVariables  *out,
			BestVariables  *in)
{

  UINT4 mObsCoh;

  INITSTATUS (status, "SelectBestStuff", rcsid);
  ATTATCHSTATUSPTR (status);

  /* check consistency of input */
  ASSERT (out, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);

  ASSERT (in, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->length > 0, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);
  mObsCoh = in->length;

  ASSERT (in->weightsV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->weightsV->length == mObsCoh, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  ASSERT (in->timeDiffV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->timeDiffV->length == mObsCoh, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  ASSERT (in->velV, status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (in->velV->length == mObsCoh, status, DRIVEHOUGHCOLOR_EBAD, DRIVEHOUGHCOLOR_MSGEBAD);

  /* memory allocation for output -- check output is null because
     function may be called within the loop over sky positions, so memory might
     already have been allocated previously */
  out->length = mObsCoh;

  if (out->weightsV == NULL) {
    out->weightsV = (REAL8Vector *)LALCalloc(1, sizeof(REAL8Vector));
    out->weightsV->length = mObsCoh;
    out->weightsV->data = (REAL8 *)LALCalloc(1, mObsCoh*sizeof(REAL8));	
  }

  if (out->timeDiffV == NULL) {
    out->timeDiffV = (REAL8Vector *)LALCalloc(1, sizeof(REAL8Vector));
    out->timeDiffV->length = mObsCoh;
    out->timeDiffV->data = (REAL8 *)LALCalloc(1, mObsCoh*sizeof(REAL8));	
  }

  if (out->velV == NULL) {
    out->velV =  (REAL8Cart3CoorVector *)LALCalloc(1, sizeof(REAL8Cart3CoorVector));
    out->velV->length = mObsCoh;
    out->velV->data = (REAL8Cart3Coor *)LALCalloc(1, mObsCoh*sizeof(REAL8Cart3Coor));
  }

  if (out->pgV == NULL) {
    out->pgV = (HOUGHPeakGramVector *)LALCalloc(1, sizeof(HOUGHPeakGramVector));
    out->pgV->length = mObsCoh;
    out->pgV->pg = (HOUGHPeakGram *)LALCalloc(1, mObsCoh*sizeof(HOUGHPeakGram));
  }

  memcpy(out->weightsV->data, in->weightsV->data, mObsCoh * sizeof(REAL8));
  memcpy(out->timeDiffV->data, in->timeDiffV->data, mObsCoh * sizeof(REAL8));
  memcpy(out->velV->data, in->velV->data, mObsCoh * sizeof(REAL8Cart3Coor));
  memcpy(out->pgV->pg, in->pgV->pg, mObsCoh * sizeof(HOUGHPeakGram));
  
  DETATCHSTATUSPTR (status);
	
  /* normal exit */	
  RETURN (status);

}
