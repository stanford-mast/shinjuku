#!/usr/bin/python

import sys
import os
import numpy as np
from scipy import stats


class Lat(object):
    def __init__(self, fileName):
        f = open(fileName, 'rb')
        a = np.fromfile(f, dtype=np.uint64)
        self.reqTimes = a.reshape((a.shape[0]/1, 1))
        f.close()

    def parseQueueTimes(self):
        return self.reqTimes[:, 0]

    def parseSvcTimes(self):
        return self.reqTimes[:, 1]

    def parseSojournTimes(self):
        return self.reqTimes[:, 0]

if __name__ == '__main__':
    def getLatPct(latsFile):
        assert os.path.exists(latsFile)

        latsObj = Lat(latsFile)

        # qTimes = [l/1e6 for l in latsObj.parseQueueTimes()]
        # svcTimes = [l/1e6 for l in latsObj.parseSvcTimes()]
        sjrnTimes = [l/1e3 for l in latsObj.parseSojournTimes()]

        print len(sjrnTimes)
        mean = np.mean(sjrnTimes)
        print mean
        median = stats.scoreatpercentile(sjrnTimes, 50)
        print median
        p95 = stats.scoreatpercentile(sjrnTimes, 95)
        print p95
        p99 = stats.scoreatpercentile(sjrnTimes, 99)
        print p99
        p999 = stats.scoreatpercentile(sjrnTimes, 99.9)
        print p999
        maxLat = max(sjrnTimes)
        print maxLat

    latsFile = sys.argv[1]
    getLatPct(latsFile)
