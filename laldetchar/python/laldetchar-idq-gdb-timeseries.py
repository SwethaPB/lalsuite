# Copyright (C) 2013 Reed Essick
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


import sys
import numpy as np
import re as re
from laldetchar.idq import event
from ligo.gracedb.rest import GraceDb

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from laldetchar.idq import idq
from laldetchar.idq import reed
from laldetchar.idq import event
from laldetchar.idq import idq_gdb_utils
from laldetchar.idq import idq_tables
from glue.ligolw import ligolw
from laldetchar.idq import idq_tables_dbutils
from glue.ligolw import utils as ligolw_utils
from glue.ligolw import lsctables
from glue.ligolw import dbtables
from glue.ligolw import table

from optparse import OptionParser

from laldetchar import git_version

#===================================================================================================

__author__ = 'Reed Essick <reed.essick@ligo.org>'
__version__ = git_version.id__date__ = git_version.date

description = \
    """ Program generates a summary of iDQ glitch-rank time-series during a short time period, \
    generating figures and summary files"""

#===================================================================================================

def get_glitch_times(glitch_xmlfiles):
    """
    Returns list of (gps,gps_ns) tuples for all events contained in the glitch_xmlfiles.
    """
    # load files in database
    connection, cursor = idq_tables_dbutils.load_xml_files_into_database(glitch_xmlfiles)

    # get table names from the database
    tablenames = dbtables.get_table_names(connection)
    if not tablenames:
        print "No tables were found in the database."
        return []
        
    # check if glitch table is present
    if not table.StripTableName(idq_tables.IDQGlitchTable.tableName) in tablenames:
        print "No glitch table is found in database."
        print "Can not perform requested query."
        return []

    data = cursor.execute('''SELECT gps, gps_ns FROM ''' + \
        table.StripTableName(idq_tables.IDQGlitchTable.tableName)).fetchall()
    # close database
    connection.close()
    return data 

def get_glitch_ovl_snglburst_summary_info(glitch_xmlfiles, glitch_columns, ovl_columns, snglburst_columns):
    """
    Generates summary info table for glitch events stored in glitch_xmlfiles.
    Returns list of (ifo, gps, gps_ns, rank, fap, ovl_channel, trig_type, trig_snr) tuples.
    Each tuple in the list corresponds to a glitch event.
    """
    # load files in database
    connection, cursor = idq_tables_dbutils.load_xml_files_into_database(glitch_xmlfiles)

    # get glitch gps times and ovl channels
    data = idq_tables_dbutils.get_get_glitch_ovl_sngburst_data(\
        connection, cursor, glitch_columns, ovl_columns, snglburst_columns)

    # close database
    connection.close()
    return data



def get_glitch_ovl_channels(glitch_xmlfiles):
    """
    Gets ovl channels for glitch events from glitch_xmlfiles.
    Returns list of (gps_seconds, gps_nanonsecons, ovl_channel) tuples.
    Each tuple in the list corresponds to a glitch event.
    """
    # load files in database
    connection, cursor = idq_tables_dbutils.load_xml_files_into_database(glitch_xmlfiles)
    
    # get glitch gps times and ovl channels
    data = idq_tables_dbutils.get_glitch_ovl_data(connection, cursor, \
        ['gps', 'gps_ns'], ['aux_channel'])
    # close database
    connection.close()
    return data


parser = OptionParser(version='Name: %%prog\n%s'% git_version.verbose_msg,
        usage='%prog [options]',
        description=description)

parser.add_option('-v',
        '--verbose',
        default=False,
        action='store_true')

parser.add_option(
        '-s',
        '--gps-start',
        dest='start',
        default=0,
        type='float',
        help='the gps start time of the time range of interest')

parser.add_option(
        '-e',
        '--gps-end',
        dest='end',
        default=0,
        type='float',
        help='the gps end time of the time range of interest')

parser.add_option('',
        '--plotting-gps-start',
        default=None,
        type='float',
        help='the gps start time of the plots. This may be before --gps-start, but cannot be after')

parser.add_option('',
        '--plotting-gps-end',
        default=None,
        type='float',
        help='the gps end time of the plots. This may be after --gps-end, but cannot be before')

parser.add_option('',
        '--gps',
        default=None,
        type='float',
        help='the timestamp of interest within [start, end]. eg: coalescence time of CBC trigger')

parser.add_option('',
        '--gch-xml',
        default=[],
        action='append',
        type='string',
        help='filename of a glitch xml file with which we annotate our plot')

parser.add_option('',
        '--cln-xml',
        default=[],
        action='append',
        type='string',
        help='filename of a clean xml file with which we annotate our plot')

parser.add_option('-g',
        '--gracedb-id',
        default=None,
        type='string',
        help='GraceDB ID')

parser.add_option('-c',
        '--classifier',
        default='ovl',
        type='string',
        help='the classifier used to generate the timeseries data. Default="ovl"')

parser.add_option('',
        '--ifo',
        type='string',
        help='the ifo for which predictions were made')

parser.add_option('-i',
        '--input-dir',
        default='./',
        type='string',
        help='the directory which is searched for relevant timeseries files. \
Assumes directory structure generated by laldetchar-idq-realtime.py')

parser.add_option('-o',
        '--output-dir',
        default='./',
        type='string',
        help='the output directory')

parser.add_option('-t',
        '--usertag',
        dest='tag',
        default='',
        type='string',
        help='user tag')

parser.add_option('',
        '--skip-gracedb-upload',
        default=False,
        action='store_true',
        help='skip steps involving communication with GraceDB')

parser.add_option('',
        '--gdb-url',
        default=False,
        type='string')

(opts, args) = parser.parse_args()

if not opts.ifo:
    opts.ifo = raw_input('ifo = ')

if opts.tag != '':
    opts.tag = opts.tag + '-'
		
if (opts.plotting_gps_start == None) or (opts.plotting_gps_start > opts.start):
    opts.plotting_gps_start = opts.start

if (opts.plotting_gps_end == None) or (opts.plotting_gps_end < opts.end):
   opts.plotting_gps_end = opts.end

if not opts.skip_gracedb_upload:
    # initialize instance of gracedb interface
    if opts.gdb_url:
        gracedb = GraceDb(opts.gdb_url)
    else:
        gracedb = GraceDb()
    # check that gracedb id is given
    if not opts.gracedb_id:
        print "GraceDB ID must be specified for enabling correct uploading of the data. Please use --gracedb-id option."
        sys.exit(1)

#===================================================================================================

# get all *.npy.gz files in range

if opts.verbose:
    print "Finding relevant *.npy.gz files"
rank_filenames = []
fap_filenames = []
all_files = idq.get_all_files_in_range(opts.input_dir, opts.plotting_gps_start,
    opts.plotting_gps_end, pad=0, suffix='.npy.gz')
for filename in all_files:
    if opts.classifier in filename and opts.ifo in filename:
        if 'rank' in filename:
            rank_filenames.append(filename)
        if 'fap' in filename:
            fap_filenames.append(filename)

rank_filenames.sort()
fap_filenames.sort()

if (not rank_filenames) or (not fap_filenames): # we couldn't find either rank or fap files
    # exit gracefully
    if opts.verbose:
        print "no iDQ timeseries for %s at %s"%(opts.classifier, opts.ifo)
    if not opts.skip_gracedb_upload:
        gracedb.writeLog(opts.gracedb_id, message="No iDQ timeseries for %s at %s"%(opts.classifier, opts.ifo))
    sys.exit(0)

#=================================================

# define plot
fig = plt.figure()
r_ax = plt.subplot(1, 1, 1)
f_ax = r_ax.twinx()
f_ax.set_yscale('log') # this may be fragile if fap=0 for all points in the plot. That's super rare, so maybe we don't have to worry about it?

r_ax.set_title(opts.ifo)

#=================================================
# RANK
#=================================================
if opts.verbose:
    print "reading rank timeseries from:"
    for filename in rank_filenames:
        print '\t' + filename

# merge time-series
if opts.verbose:
    print "merging rank timeseries"
(r_times, r_timeseries) = reed.combine_ts(rank_filenames)

# for each bit of continuous data:
#   add to plot
#   write merged timeseries file
#   generate and write summary statistics
if opts.verbose:
    print "plotting and summarizing rank timeseries"

merged_rank_filenames = []
rank_summaries = []
max_rank = -np.infty
max_rank_segNo = 0
segNo = 0
end = opts.plotting_gps_start
dur = 0.0
for (t, ts) in zip(r_times, r_timeseries):

    # ensure time series only fall within desired range........
    ts = ts[(opts.plotting_gps_start <= t) * (t <= opts.plotting_gps_end)]
    t = t[(opts.plotting_gps_start <= t) * (t <= opts.plotting_gps_end)]

    _start = round(t[0])
    _end = round(t[-1])
    _dur = _end - _start
    dur += _dur

    # add to plot
    r_ax.plot(t - opts.plotting_gps_start, ts, color='r', alpha=0.75)

    # WARNING: assumes rank segments are the same as FAP segments
    if (end!=_start):  # shade areas where there is no data
        r_ax.fill_between(
            [end - opts.plotting_gps_start, _start - opts.plotting_gps_start],
            np.zeros((2, )),
            np.ones((2, )),
            color='k',
            edgecolor='none',
            alpha=0.2,
            )
    end = _end

    # write merged timeseries file
    merged_rank_filename = '%s/%s_idq_%s_rank_%s%d-%d.npy.gz' % (
        opts.output_dir,
        opts.ifo,
        opts.classifier,
        opts.tag,
        int(_start),
        int(_dur))

    if opts.verbose:
        print "\twriting " + merged_rank_filename
    np.save(event.gzopen(merged_rank_filename, 'w'), ts)
    merged_rank_filenames.append(merged_rank_filename)

    # generate and write summary statistics
    (r_min, r_max, r_mean, r_stdv) = reed.stats_ts(ts)
    if r_max > max_rank:
        max_rank = r_max
        max_rank_segNo = segNo
    rank_summaries.append([
        _start,
        _end,
        _dur / (len(t) - 1),
        r_min,
        r_max,
        r_mean,
        r_stdv,
        ])

    segNo += 1

# shade any trailing time
r_ax.fill_between( [end-opts.plotting_gps_start, opts.plotting_gps_end-opts.plotting_gps_start],
    np.zeros((2, )),
    np.ones((2, )),
    color='k',
    edgecolor='none',
    alpha=0.2,
    )

# upload rank timeseries to graceDB
if not opts.skip_gracedb_upload:
    # write log messages to gracedb and upload rank files
    for filename in merged_rank_filenames:
        gracedb.writeLog(opts.gracedb_id, message="iDQ glitch-rank timeseries for " + opts.classifier +\
                        " at "+opts.ifo+":", filename=filename)

#=================================================
# FAP
#=================================================
# Find relevant files
if opts.verbose:
    print "reading fap timeseries from:"
    for filename in fap_filenames:
        print '\t' + filename

# merge time-series
if opts.verbose:
    print "merging fap timeseries"
(f_times, f_timeseries) = reed.combine_ts(fap_filenames)
fUL_timeseries = [l[1] for l in f_timeseries]
f_timeseries = [l[0] for l in f_timeseries]

# for each bit of continuous data:
#   add to plot
#   write merged timeseries file
#   generate and write summary statistics
if opts.verbose:
    print "plotting and summarizing fap timeseries"

merged_fap_filenames = []
fap_summaries = []
min_fap = np.infty
min_fap_segNo = 0
segNo = 0
for (t, ts, ul) in zip(f_times, f_timeseries, fUL_timeseries):
    # ensure time series only fall within desired range
    ts = ts[(opts.plotting_gps_start <= t) * (t <= opts.plotting_gps_end)]
    t = t[(opts.plotting_gps_start <= t) * (t <= opts.plotting_gps_end)]

    _start = round(t[0])
    _end = round(t[-1])

    # add to plot...
    if (ts > 0).any():
        f_ax.semilogy(t - opts.plotting_gps_start, ts, color='b', alpha=0.75) ### point estimate
        f_ax.semilogy(t - opts.plotting_gps_start, ul, color='b', alpha=0.5, linestyle=":") ### upper limit
    else:
        if opts.verbose:
            print "No non-zero FAP values between %d and %d" % (_start, _end)

    # WARNING: assumes rank segments are the same as FAP segments (and therefore already plotted)

    # write merged timeseries file
    merged_fap_filename = '%s/%s_idq_%s_fap_%s%d-%d.npy.gz' % (
        opts.output_dir,
        opts.ifo,
        opts.classifier,
        opts.tag,
        int(_start),
        int(_end - _start))

    if opts.verbose:
        print "\twriting " + merged_fap_filename
    np.save(event.gzopen(merged_fap_filename, 'w'), (ts, ul))
    merged_fap_filenames.append(merged_fap_filename)

    # generate and write summary statistics
    (f_min, f_max, f_mean, f_stdv) = reed.stats_ts(ts)
    if f_min < min_fap:
        min_fap = f_min
        min_fap_segNo = segNo
    fap_summaries.append([
        _start - opts.plotting_gps_start,
        _end - opts.plotting_gps_start,
        (_end - _start) / (len(t) - 1),
        f_min,
        f_max,
        f_mean,
        f_stdv,
        ])

    segNo += 1

# upload to graceDB
if not opts.skip_gracedb_upload:
    # write log messages to gracedb and upload fap files
    for filename in merged_fap_filenames:
        gracedb.writeLog(opts.gracedb_id, message="iDQ fap timeseries for %s at %s:"%(opts.classifier, opts.ifo),
                        filename=filename)


#=================================================
# finish plot

r_ax.set_ylabel('%s rank' % opts.classifier, color='r')
f_ax.set_ylabel('%s FAP' % opts.classifier, color='b')

#r_ax.set_ylabel("$\mathrm{rank}_{\mathrm{%s}}$" % opts.classifier)
#f_ax.set_ylabel("$\mathrm{FAP}_{\mathrm{%s}}$" % opts.classifier)

r_ax.set_xlabel('time [seconds after %d]' % opts.plotting_gps_start)

r_ax.set_xlim(xmin=0, xmax=opts.plotting_gps_end - opts.plotting_gps_start)
f_ax.set_xlim(r_ax.get_xlim())

r_ymin = -1e-2
r_ymax = 1.01
r_ax.set_ylim(ymin=r_ymin, ymax=r_ymax)

f_ymin = max(1e-7, min(min_fap, 1e-2))
f_ymax = 10 ** (1e-2 / 1.02 * np.log10(1. / f_ymin))
f_ax.set_ylim(ymin=f_ymin, ymax=f_ymax)

# annotate specified gps
if opts.gps!=None:
    r_ax.plot((opts.gps - opts.plotting_gps_start) * np.ones((2, )),
          r_ax.get_ylim(), ':k', linewidth=2, alpha=0.5)
    r_ax.text(opts.gps - opts.plotting_gps_start, 0.8, str(opts.gps), ha='center',
          va='center')

if opts.gch_xml:
    ### annotate glitches
    gps_times = [ gps + gps_ns * 1e-9 for (gps, gps_ns) in get_glitch_times(opts.gch_xml)]
    gps_times = np.asarray(gps_times)
    # channel annotation looks awfull on the static plot, must find a better way to visualize it
    #r_ax.text(gps - opts.plotting_gps_start, 0.1, auxchannel, ha="left", va="bottom", rotation=45)
    r_ax.plot(gps_times - opts.plotting_gps_start, 0.1*np.ones(len(gps_times)), marker="x", markerfacecolor="g", \
        markeredgecolor = "g", linestyle="none")


### shade region outside of opts.start, opts.end
if opts.start != opts.plotting_gps_start:
    r_ax.fill_between( [0, opts.start-opts.plotting_gps_start],
        np.zeros((2, )),
        np.ones((2, )),
        color='k',
        edgecolor='none',
        alpha=0.1,
        )
if opts.end != opts.plotting_gps_end:
    r_ax.fill_between( [opts.end-opts.plotting_gps_start, opts.plotting_gps_end-opts.plotting_gps_start],
        np.zeros((2, )),
        np.ones((2, )),
        color='k',
        edgecolor='none',
        alpha=0.1,
        )

# save plot
plt.setp(fig, figwidth=10, figheight=4)

figname = '%s/%s_idq_%s_rank_fap_%s%d-%d.png' % (
        opts.output_dir,
        opts.ifo,
        opts.classifier,
        opts.tag,
        int(opts.plotting_gps_start),
        int(opts.plotting_gps_end - opts.plotting_gps_start))
if opts.verbose:
    print '\tsaving ' + figname
fig.savefig(figname)
plt.close(fig)

if not opts.skip_gracedb_upload:
    ### write log message to gracedb and upload file
    gracedb.writeLog(opts.gracedb_id, message="iDQ fap and glitch-rank timeseries plot for " + opts.classifier +\
                    " at "+opts.ifo+":", filename=figname, tagname='data_quality')

#=================================================
# write summary file
summary_filename = '%s/%s_idq_%s_summary_%s%d-%d.txt' % (
        opts.output_dir,
        opts.ifo,
        opts.classifier,
        opts.tag,
        int(opts.plotting_gps_start),
        int(opts.plotting_gps_end - opts.plotting_gps_start))
if opts.verbose:
    print "\twriting " + summary_filename
summary_file = open(summary_filename, 'w')

# WARNING: Summary file assumes fap segments are identical to rank segments!

No_segs = len(merged_rank_filenames)
print >> summary_file, 'coverage : %f' % (dur / (opts.plotting_gps_end - opts.plotting_gps_start))
print >> summary_file, 'No. segments : %d' % No_segs
print >> summary_file, 'max_rank : %f' % max_rank
print >> summary_file, 'max_rank_segNo : %d' % max_rank_segNo
print >> summary_file, 'min_fap : %f' % min_fap
print >> summary_file, 'min_fap_segNo : %d' % min_fap_segNo

for segNo in range(No_segs):
    print >> summary_file, '\nsegNo : %d' % segNo
    (_start,
        _end,
        _dt,
        r_min,
        r_max,
        r_mean,
        r_stdv,
        ) = rank_summaries[segNo]
    print >> summary_file, '\tstart : %d' % _start
    print >> summary_file, '\tend : %d' % _end
    print >> summary_file, '\tdt : %f' % _dt

    print >> summary_file, '\tmerged_rank_filename : ' \
        + merged_rank_filenames[segNo]
    print >> summary_file, '\tmin rank : %f' % r_min
    print >> summary_file, '\tmax rank : %f' % r_max
    print >> summary_file, '\tmean rank : %f' % r_mean
    print >> summary_file, '\tstdv rank : %f' % r_stdv

    (_,
        _,
        _,
        f_min,
        f_max,
        f_mean,
        f_stdv,
        ) = fap_summaries[segNo]
    print >> summary_file, '\tmerged_fap_filename : ' \
        + merged_fap_filenames[segNo]
    print >> summary_file, '\tmin fap : %f' % f_min
    print >> summary_file, '\tmax fap : %f' % f_max
    print >> summary_file, '\tmean fap : %f' % f_mean
    print >> summary_file, '\tstdv fap : %f' % f_stdv

    if opts.classifier == 'ovl' and opts.gch_xml:
        # write summary info using glitch, ovl and snglburst tables 
        glitch_columns = ['ifo','gps', 'gps_ns']
        ovl_columns = ['aux_channel']
        snglburst_columns = ['search', 'snr']
        # get the data from xml tables
        summary_data = get_glitch_ovl_snglburst_summary_info(opts.gch_xml, glitch_columns,\
            ovl_columns, snglburst_columns)

        print >> summary_file, '\n'
        print >> summary_file, 'OVL Glitch Events Summary Table'
        # write header
        print >> summary_file, '| #  | ' + ' | '.join(glitch_columns) + ' | ' + ' | '.join(ovl_columns) + ' | ' + ' | '.join(snglburst_columns) + ' |'
        # write data
        for (i, row) in enumerate(summary_data):
            print >> summary_file,  '| ' + str(i) + ' | ' + ' | '.join([str(v) for v in row])


summary_file.close()

if not opts.skip_gracedb_upload:
    ### write log message to gracedb and upload file
    gracedb.writeLog(opts.gracedb_id, message="iDQ timeseries summary for " + opts.classifier +\
                    " at "+opts.ifo+":", filename=summary_filename)

    #===================================================================================================
    # compute statistics within specified window (opts.start, opts.end)
    min_fap = 1.0
    for (t, ts) in zip(f_times, f_timeseries):
        # ensure time series only fall within desired range
        ts = ts[(opts.start <= t) * (t <= opts.end)]
        t = t[(opts.start <= t) * (t <= opts.end)]

        if len(t): # some surviving data
            # generate and write summary statistics
            (f_min, f_max, f_mean, f_stdv) = reed.stats_ts(ts)

            # update min_fap
            if min_fap > f_min:
                min_fap = f_min

    # upload minimum fap observed within opts.start, opts.end
    if min_fap > 0:
        b = int(np.floor(np.log10(min_fap)))
        a = min_fap*(10**-b)
    else:
        a = b = 0
    gracedb.writeLog(opts.gracedb_id, message="minimum glitch-FAP for "+opts.classifier+" at "+opts.ifo+" within [%.3f, %.3f] is %.3fe%d"%(opts.start, opts.end, a,b), tagname='data_quality')

if opts.verbose:
    print "Done"

