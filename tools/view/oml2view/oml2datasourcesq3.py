#!/usr/bin/env python2
#
# Copyright 2012 National ICT Australia (NICTA), Australia
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

__revision__ = "@PACKAGE_VERSION@"

from pysqlite2 import dbapi2 as sqlite3

class Oml2DataSourceSq3:
    def __init__(self, dbfile):
        self._connection = sqlite3.connect(dbfile)

    def tables_list(self):
        c = self._connection.cursor()
        c.execute("select name from SQLite_Master \
                          where type = 'table' and \
                          name != '_senders' and name != '_experiment_metadata'")
        l = []
        for r in c:
            l.append(r[0])
        return l

    def table_schema(self, table):
        c = self._connection.cursor()
        #c.execute("select * from '?' limit 0", (table,)) 
        # XXX: Open to SQL injections
        c.execute("select * from '%s' limit 1" % (table,)) 
        d = { }
        i = 0
        r = c.fetchone()
        for row in c.description:
            d[row[0]] = type(r[i])
            i = i+1
        return d

    def table_data(self, table, columns, labels=[]):
        c = self._connection.cursor()
        filter = []
        d = { }
        if len(labels) > 0:
            for l in reversed(labels): # We are going to remove things; we don't want to mess up with internal iterators
                # Find labels with more than one value
                # XXX: Open to SQL injections
                c.execute("select distinct %s from '%s'" % (l, table))
                ret = c.fetchall()
                if len(ret)<2:
                    print table, 'uninteresting label', l
                    labels.remove(l)
            print table, 'interesting labels', labels
        if len(labels) > 0:
            # XXX: Open to SQL injections
            c.execute("select distinct %s from '%s'" % (", ".join(labels), table))
            for row in c:
                f = [] # TODO: put list in dict
                i = 0
                for col in row:
                    f.append(c.description[i][0] + "='" + col + "'")
                    i = i + 1
                filter.append(f)

            for f in filter:
                print "select %s from '%s' where %s" % (", ".join(columns), table, " and ".join(f))
                # XXX: Open to SQL injections
                c.execute("select %s from '%s' where %s" % (", ".join(columns), table, " and ".join(f)))
                for col in columns:
                    print col + "-" + "-".join(f)
                    d[col] = []
                for row in c:
                    i = 0
                    for col in row:
                        d[columns[i]].append(col)
                        i=i+1
        else:
            # XXX: Open to SQL injections
            c.execute("select %s from '%s'" % (", ".join(columns), table))
            for col in columns:
                d[col] = []
            for row in c:
                i = 0
                for col in row:
                    d[columns[i]].append(col)
                    i=i+1
        return d

    def senders_list(self):
        c = self._connection.cursor()
        c.execute("select * from _senders")
        d = {}
        for r in c:
            d[r[1]] = r[0]
        return d

    def metadata_list(self):
        c = self._connection.cursor()
        c.execute("select * from _experiment_metadata")
        d = {}
        for r in c:
            d[r[0]] = r[1]
        return d
