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

import gobject
import gtk
import matplotlib
matplotlib.use('GTKAgg')  # or 'GTK'
from matplotlib.backends.backend_gtk import FigureCanvasGTK as FigureCanvas
from matplotlib.figure import Figure


(
    COLUMN_FIELD,
    COLUMN_TYPE,
    COLUMN_SHOW,
    COLUMN_IS_LABEL,
    COLUMN_IS_X,
) = range(5)

class Oml2GraphPane(gtk.VBox):
    def __init__(self, datasource, table):
        gtk.VBox.__init__(self)
        self._datasource = datasource
        self._table = table
        self._plots = []

        self._fig = Figure()
        self._graph = FigureCanvas(self._fig) #gtk.Label("placeholder for %s graph" % self._table)
        self.pack_start(self._graph)
        
        sw = gtk.ScrolledWindow()
        sw.set_policy(gtk.POLICY_NEVER, gtk.POLICY_AUTOMATIC)
        self.pack_end(sw)
        self._treeview = gtk.TreeView(self._create_model())
        self._add_columns()
        sw.add(self._treeview)

        self.replot()

        self.show_all()

    def _create_model(self):
        lst = gtk.ListStore(
                gobject.TYPE_STRING,
                gobject.TYPE_STRING,
                gobject.TYPE_BOOLEAN,
                gobject.TYPE_BOOLEAN,
                gobject.TYPE_BOOLEAN)

        schema = self._datasource.table_schema(self._table)
        for k in schema.keys():
            it = lst.append()
            lst.set(it,
                    COLUMN_FIELD, k,
                    COLUMN_TYPE, schema[k],
                    COLUMN_SHOW, schema[k] in (int, float) and
                        k not in ('oml_sender_id', 'oml_ts_server', 'oml_ts_client', 'oml_seq'),
                    COLUMN_IS_LABEL, schema[k] in (str, unicode),
                    COLUMN_IS_X, k == 'oml_ts_server',
                    )

        return lst

    def replot(self):
        x = self.get_x()
        ys = self.get_ys()
        labels = self.get_labels()

        columns = []
        columns.append(x)
        columns.extend(ys)
        d = self._datasource.table_data(self._table, columns, labels)

        self._fig.clear()
        sp = self._fig.add_subplot(111)
        #sp.xlabel(x)
        for y in ys:
            sp.plot(d[x], d[y], '-', label=y, linewidth=2) 
        sp.legend()

    def get_x(self):
        model = self._treeview.get_model()
        i = 0
        try:
            while True:
                iter = model.get_iter(i)
                if model.get_value(iter, COLUMN_IS_X):
                    return model.get_value(iter, COLUMN_FIELD)
                i = i + 1
        except ValueError:
            pass
        return 'oml_ts_server'

    def get_ys(self):
        model = self._treeview.get_model()
        l = []
        i = 0
        try:
            while True:
                iter = model.get_iter(i)
                if model.get_value(iter, COLUMN_SHOW) and \
                        not model.get_value(iter, COLUMN_IS_X) and \
                        not model.get_value(iter, COLUMN_IS_LABEL):
                    l.append(model.get_value(iter, COLUMN_FIELD))
                i = i + 1
        except ValueError:
            pass
        return l

    def get_labels(self):
        model = self._treeview.get_model()
        l = []
        i = 0
        try:
            while True:
                iter = model.get_iter(i)
                if model.get_value(iter, COLUMN_IS_LABEL) and \
                        not model.get_value(iter, COLUMN_IS_X):
                    l.append(model.get_value(iter, COLUMN_FIELD))
                i = i + 1
        except ValueError:
            pass
        return l

    def _add_columns(self):
        model = self._treeview.get_model()

        # column for field name
        column = gtk.TreeViewColumn('Field', gtk.CellRendererText(),
                                    text=COLUMN_FIELD)
        column.set_sort_column_id(COLUMN_FIELD)
        self._treeview.append_column(column)

        # column for field type
        column = gtk.TreeViewColumn('Type', gtk.CellRendererText(),
                                    text=COLUMN_TYPE)
        column.set_sort_column_id(COLUMN_TYPE)
        self._treeview.append_column(column)

        # column for show checkbox
        renderer = gtk.CellRendererToggle()
        renderer.connect('toggled', self._show_cb, model)
        column = gtk.TreeViewColumn('Show', renderer,
                                     active=COLUMN_SHOW)
        column.set_sort_column_id(COLUMN_SHOW)
        self._treeview.append_column(column)

        # column for label checkbox
        renderer = gtk.CellRendererToggle()
        renderer.connect('toggled', self._is_label_cb, model)
        column = gtk.TreeViewColumn('Use as label', renderer,
                                    active=COLUMN_IS_LABEL)
        column.set_sort_column_id(COLUMN_IS_LABEL)
        self._treeview.append_column(column)

        # column for label checkbox
        renderer = gtk.CellRendererToggle()
        renderer.set_radio(True)
        renderer.connect('toggled', self._is_x_cb, model)
        column = gtk.TreeViewColumn('X axis', renderer,
                                    active=COLUMN_IS_X)
        column.set_sort_column_id(COLUMN_IS_X)
        self._treeview.append_column(column)

    def _show_cb(self, cell, path, model):
        iter = model.get_iter((int(path),))
        value = model.get_value(iter, COLUMN_SHOW)
        value = not value
        model.set(iter, COLUMN_SHOW, value)
        self.replot()

    def _is_label_cb(self, cell, path, model):
        iter = model.get_iter((int(path),))
        value = model.get_value(iter, COLUMN_IS_LABEL)
        value = not value
        model.set(iter, COLUMN_IS_LABEL, value)
        self.replot()

    def _is_x_cb(self, cell, path, model):
        i = 0
        try:
            while True:
                iter = model.get_iter(i)
                if model.get_value(iter, COLUMN_IS_X):
                    model.set_value(iter, COLUMN_IS_X, False)
                    break
                i = i + 1
        except ValueError:
            pass
        
        iter = model.get_iter((int(path),))
        value = model.get_value(iter, COLUMN_IS_X)
        value = not value
        model.set(iter, COLUMN_IS_X, value)
        self.replot()

