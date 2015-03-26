# Copyright (C) 2014 Reed Essick
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
#

usage = "laldetchar-idq-calibration.py [--options]"
description = \
"""
Written to estimate mappings between classifier rank and FAP, Eff, Likelihood, etc. rapidly using historical performance data.
This should be run often to ensure an accurate calibration of the realtime processes, and such jobs will be scheduled and launched from within the realtime process.

Will also check how accurate the previous calibrations have been by computing segments from FAP timeseries.
We expect that segments generated by thresholding on the FAP time-series at FAP=0.XYZ should correspond to an amount of time DURATION=0.XYZ*LIVETIME
"""

#===================================================================================================

import os
import sys

import numpy as np

from collections import defaultdict

import ConfigParser
from optparse import OptionParser

#from laldetchar.idq import idq
from laldetchar.idq import reed
from laldetchar.idq import event
from laldetchar.idq import calibration

from laldetchar import git_version

__author__ = 'Reed Essick <reed.essick@ligo.org>'
__version__ = git_version.id
__date__ = git_version.date

#===================================================================================================

parser = OptionParser(version='Name: %%prog\n%s'%git_version.verbose_msg,
                          usage=usage,
                          description=description)

parser.add_option('-v', '--verbose', default=False, action='store_true')

parser.add_option('-c', '--config', default='idq.ini', type='string', help='configuration file')

parser.add_option('-l', '--log-file', default='idq_calibration.log', type='string', help='log file')

parser.add_option('-s', '--gps-start', dest="gpsstart", default=-np.infty, type='float', help='gps start time')
parser.add_option('-e', '--gps-stop', dest="gpsstop", default=np.infty, type='float', help='gps end time')

parser.add_option('-b', '--lookback', default='0', type='string', help="Number of seconds to look back and get data for training. Default is zero.\
        Can be either positive integer or 'infinity'. In the latter case, the lookback will be incremented at every stride and all data after --gps-start will be used in every training.")

parser.add_option('-f','--force',default=False, action='store_true', help="forces *uroc cache file to be updated, even if we have no data. Use with caution.")

parser.add_option("", "--ignore-science-segments", default=False, action="store_true")
parser.add_option("", "--no-robot-cert", default=False, action="store_true")

parser.add_option('', '--FAPthr', default=[], action="append", type='float', help='check calibration at this FAP value. This argument can be supplied multiple times to check multiple values.')

opts, args = parser.parse_args()

if opts.lookback != "infinity":
    lookback = int(train_lookback)

#===================================================================================================
### setup logger to record processes
logger = reed.setup_logger('idq_logger', opts.log_file, sys.stdout, format='%(asctime)s %(message)s')

sys.stdout = reed.LogFile(logger)
sys.stderr = reed.LogFile(logger)

#===================================================================================================
### read global configuration file

config = ConfigParser.SafeConfigParser()
config.read(opts.config)

#mainidqdir = config.get('general', 'idqdir') ### get the main directory where idq pipeline is going to be running.

ifo = config.get('general', 'ifo')

usertag = config.get('general', 'usertag')
if usertag:
    usertag = "_%s"%usertag

#========================
# which classifiers
#========================
classifiers = sorted(set(config.get('general', 'classifiers').split()))
if not classifiers:
    raise ValueError("no classifiers in general section of %s"%opts.config_file)

### ensure we have a section for each classifier and fill out dictionary of options
classifiersD, mla, ovl = reed.config_to_classifiersD( config )

if mla:
    ### reading parameters from config file needed for mla
    auxmvc_coinc_window = config.getfloat('build_auxmvc_vectors','time-window')
    auxmc_gw_signif_thr = config.getfloat('build_auxmvc_vectors','signif-threshold')
    auxmvc_selected_channels = config.get('general','selected-channels')
    auxmvc_unsafe_channels = config.get('general','unsafe-channels')

#========================
# realtime
#========================
realtimedir = config.get('general', 'realtimedir')

#========================
# calibration
#========================
calibrationdir = config.get('general', 'calibrationdir')

stride = config.getint('calibration', 'stride')
delay = config.getint('calibration', 'delay')

calibration_cache = dict( (classifier, reed.Cachefile(reed.cache(calibrationdir, classifier, tag='_calibration%s'%usertag))) for classifier in classifiers )

min_num_gch = config.getfloat('calibration', 'min_num_gch')
min_num_cln = config.getfloat('calibration', 'min_num_cln')

emaillist = config.get('warnings', 'calibration')
errorthr = config.getfloat('warnings', 'calibration_errorthr')

uroc_nsamples = config.getint('calibration','urank_nsamples')
urank = np.linspace(0, 1, uroc_nsamples) ### uniformly spaced ranks used to sample ROC curves -> uroc

### used for calibration check output files
report_str = \
"""
        FAPthr   = %.5E
      stated FAP = %.5E
       deadtime  = %.5E
   \% difference = %.3E\%
   UL stated FAP = %.5E
     UL deadtime = %.5E
UL \% difference = %.5E\%
"""

#========================
# data discovery
#========================
if not opts.ignore_science_segments:
    ### load settings for accessing dmt segment files
#    dmt_segments_location = config.get('get_science_segments', 'xmlurl')
    dq_name = config.get('get_science_segments', 'include').split(':')[1]
    segdb_url = config.get('get_science_segments', 'segdb')

#==================================================
### set up ROBOT certificates
### IF ligolw_segement_query FAILS, THIS IS A LIKELY CAUSE
if opts.no_robot_cert:
    logger.warning("Warning: running without a robot certificate. Your personal certificate may expire and this job may fail")
else:
    ### unset ligo-proxy just in case
    if os.environ.has_key("X509_USER_PROXY"):
        del os.environ['X509_USER_PROXY']

    ### get cert and key from ini file
    robot_cert = config.get('ldg_certificate', 'robot_certificate')
    robot_key = config.get('ldg_certificate', 'robot_key')

    ### set cert and key
    os.environ['X509_USER_CERT'] = robot_cert
    os.environ['X509_USER_KEY'] = robot_key

#==================================================
### current time and boundaries

t = int(reed.nowgps())

gpsstop = opts.gpsstop
if not gpsstop: ### stop time of this analysis
    logger.info('computing gpsstop from current time')
    gpsstop = t ### We do not require boundaries to be integer multiples of stride

gpsstart = opts.gpsstart
if not gpsstart:
    logger.info('computing gpsstart from gpsstop')
    gpsstart = gpsstop - stride

#===================================================================================================
#
# LOOP
#
#===================================================================================================
logger.info('Begin: calibration')

### wait until all jobs are finished
wait = gpsstart + stride + delay - t
if wait > 0:
    logger.info('----------------------------------------------------')
    logger.info('waiting %.1f seconds to reach gpsstop+delay=%d' % (wait, delay))
    time.sleep(wait)

global_start = gpsstart

### iterate over all ranges
while gpsstart < gpsstop:

    logger.info('----------------------------------------------------')

    wait = gpsstart + stride + delay - t
    if wait > 0:
        logger.info('waiting %.1f seconds to reach gpsstart+stride+delay=%d' %(wait, gpsstart+stride+delay))
        time.sleep(wait)

    logger.info('Begin: stride [%d, %d]'%(gpsstart, gpsstart+stride))

    ### directory into which we write data
    output_dir = "%s/%d_%d/"%(calibrationdir, gpsstart, gpsstart + stride)
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    if opts.lookback=="infinity":
        lookback = gpsstart - global_start

    #===============================================================================================
    # science segments
    # we query the segdb right now, although that latency may be an issue...
    #===============================================================================================
    if opts.ignore_science_segments:
        logger.info('analyzing data regardless of science segements')
        scisegs = [[gpsstart-lookback], [gpsstart+stride]] ### set segs to be this stride range
        coveredsegs = [[gpsstart-lookback], [gpsstart+stride]] ### set segs to be this stride range

    else:
        logger.info('Begin: querrying science segments')

        try:
            ### this returns a string
            seg_xml_file = reed.segment_query(config, gpsstart - lookback , gpsstart + stride, url=segdb_url)

            ### write seg_xml_file to disk
            lsctables.use_in(ligolw.LIGOLWContentHandler)
            xmldoc = ligolw_utils.load_fileobj(seg_xml_file, contenthandler=ligolw.LIGOLWContentHandler)[0]

            ### science segments xml filename
            seg_file = reed.segxml(output_dir, "_%s"%dq_name, gpsstart - lookback , lookback+stride)

            logger.info('writing science segments to file : '+seg_file)
            ligolw_utils.write_filename(xmldoc, seg_file, gz=seg_file.endswith(".gz"))

            (scisegs, coveredseg) = reed.extract_dq_segments(seg_file, dq_name) ### read in segments from xml file

        except Exception as e:
            traceback.print_exc()
            logger.info('ERROR: segment generation failed. Skipping this training period.')

            if opts.force: ### we are require successful training or else we want errors
                logger.info(traceback.print_exc())
                raise e
            else: ### we don't care if any particular training job fails
                gpsstart += stride
                continue

    logger.info('finding idq segments')
    idqsegs = reed.get_idq_segments(realtimedir, gpsstart-lookback, gpsstart+stride, suffix='.dat')

    logger.info('taking intersection between science segments and idq segments')
    idqsegs = event.andsegments( [scisegs, idqsegs] )

    ### write segment file
    if opts.ignore_science_segments:
        idqseg_path = reed.idqsegascii(output_dir, '', gpsstart-lookback, lookback+stride)
    else:
        idqseg_path = reed.idqsegascii(output_dir, '_%s'%dq_name, gpsstart - lookback, lookback+stride)
    f = open(idqseg_path, 'w')
    for seg in idqsegs:
        print >> f, seg[0], seg[1]
    f.close()

    #===============================================================================================
    # update mappings via uroc files
    #===============================================================================================

    ### find all *dat files, bin them according to classifier
    logger.info('finding all *dag files')
    datsD = defaultdict( list )
    for dat in reed.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.dat' ):
        datsD[reed.extract_dat_name( dat )].append( dat )

    ### throw away any un-needed files
    for key in datsD.keys():
        if key not in classifiers:
            datsD.pop(key) 
        else: ### throw out files that don't contain any science time
            datsD[key] = [ dat for dat in datsD[key] if event.livetime(event.andsegments([idqsegs, reed.extract_start_stop(dat, suffix='.dat')])) ]

    #====================
    # update uroc for each classifier
    #====================
    for classifier in classifiers:
        ### write list of dats to cache file
        cache = reed.cache(output_dir, classifier, "_datcache%s"%usertag)
        logger.info('writing list of dat files to %s'%cache)
        f = open(cache, 'w')
        for dat in datsD[classifier]:
            print >>f, dat
        f.close()

        logger.info('  computing new calibration for %s'%classifier)

        ### extract data from dat files
        output = reed.slim_load_datfiles(datsD[classifier], skip_lines=0, columns='GPS i rank'.split())

        ### filter times by scisegs -> keep only the ones within scisegs
        out = np.array(event.include( [ [output['GPS'][i], output['i'][i], output['rank'][i] ] for i in xrange(len(output['GPS'])) ], idqsegs, tcent=0 ))
        if not len(out): ### no data remains!
            output['GPS'] = []
            output['i'] = []
            output['rank'] = []
        else:
            output['GPS'] = out[:,0]
            output['i'] = out[:,1]
            output['rank'] = out[:,2]

        ### define weights over time
        output['weight'] = calibration.weights( output['GPS'], weight_type="uniform" )

        ### compute rcg from output
        r, c, g = reed.dat_to_rcg( output )

        if opts.force or ((c[-1] >= min_num_cln) and (g[-1] >= min_num_gch)):

            ### dump into roc file
            roc = reed.roc(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
            logger.info('  writting %s'%roc)
            reed.rcg_to_file(roc, r, c, g)

            ### upsample to roc
            r, c, g = reed.resample_rcg(urank, r, c, g)
 
            ### dump uroc to file
            uroc = reed.uroc(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
            logger.info('  writing %s'%uroc)
            reed.rcg_to_file(uroc, r, c, g)

            ### update cache file
            logger.info('  adding %s to %s'%(uroc, calibration_cache[classifier].name) )
            calibration_cache[classifier].append( uroc )

        else:
            logger.warning('WARNING: not enough samples to trust calibration. skipping calibration update for %s'%classifier)

    #===============================================================================================
    # check historical calibration, send alerts
    #===============================================================================================
    if opts.FAPthr: ### only if we have something to do
        logger.info('checking historical calibration for accuracy')

        ### find all *fap*npy.gz files, bin them according to classifier
        logger.info('    finding all *fap*.npy.gz files')
        fapsD = defaultdict( list )
        for fap in [fap for fap in  reed.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.npy.gz') if "fap" in fap]:
            fapsD[reed.extract_fap_name( dat )].append( fap )

        ### throw away files we will never need
        for key in fapsD.keys():
            if key not in classifiers: ### throw away unwanted files
                fapsD.pop(key)
            else: ### keep only files that overlap with scisegs
                fapsD[key] = [ dat for dat in datsD[key] if event.livetime(event.andsegments([idqsegs, reed.extract_start_stop(dat, suffix='.npy.gz')])) ]

        ### iterate through classifiers
        alerts = {} ### files that we should be alerted about
        for classifier in classifiers:
            logger.info('  checking calibration for %s'%classifier)

            _times, timeseries = reed.combine_ts(fapsD[classifier]) ### read in time-series

            times = []
            faps = []
            fapsUL = []
            for t, ts in zip(_times, timeseries):
                _t, _ts = reed.timeseries_in_segments(t, ts, idqsegs)
                if len(_ts):
                    times.append( _t )
                    faps.append( _ts[0] )
                    fapsUL.append( _ts[1] )

            ### check point estimate calibration
            _, deadtimes, statedFAPs = calibration.check_calibration(idqsegs, times, faps, opts.FAPthr) 
            errs = np.array([ d/F - 1.0 for d, F in zip(deadtimes, statedFAPs) if d or F ])

            ### check UL estimate calibration
            _, deadtimesUL, statedFAPsUL = calibration.check_calibration(idqsegs, times, fapsUL, opts.FAPthr)
            errsUL = np.array([ d/F - 1.0 for d, F in zip(deadtimesUL, statedFAPs) if d or F ])

            calib_check = reed.calib_check(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
            logger.info('  writing %s'%calib_check)            

            file_obj = open(calib_check, "w")
            print >> file_obj, "livetime = %.3f"%event.livetime(idqsegs)
            for FAPthr, deadtime, statedFAP, deadtimeUL, statedFAPUL in zip(opts.FAPthr, deadtimes, statedFAPs, deadtimesUL, statedFAPsUL):
                if (deadtime or statedFAP) and (deadtimeUL or statedFAPUL):
                    print >> file_obj, report_str%(FAPthr, statedFAP, deadtime, 100 * (deadtime / statedFAPthr - 1), statedFAPUL, deadtimeUL, 100 * (deadtimeUL / statedFAPUL - 1.0) )
            file_obj.close()

            if np.any(np.abs(errs) > errorthr) or np.any(np.abs(errsUL) > errorthr):
                alerts[classifier] = calib_check

        if alerts: ### there are some interesting files
            alerts_keys = sorted(alerts.keys())
            alerts_keys_str = " ".join(alerts_keys)
            logger.warning('WARNING: found suspicous historical calibrations for : %s'%alerts_keys_str )
            if emaillist:
                email_cmd = "echo \"calibration check summary files are attached for: %s\" | mailx -s \"%s idq%s calibration warning\" %s \"%s\""%(alerts_keys_str, ifo, usertag, " ".join("-a \"%s\""%alerts[key] for key in alerts_keys), emaillist)
                logger.warning("  %s"%email_cmd)
                exit_code = os.system( email_cmd )
                if exit_code:
                    logger.warning("WARNING: failed to send email!")

    """
    check ROC curves, channel statistics, etc. send alerts if something changes?
    """

    logger.info('Done: stride [%d, %d]'%(gpsstart, gpsstart+stride))

    gpsstart += stride

