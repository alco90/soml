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

import gtk

from oml2datasourcesq3 import Oml2DataSourceSq3
from oml2graphpane import Oml2GraphPane

class Oml2View(gtk.Notebook):
    def __init__(self, datasource=None):
        gtk.Notebook.__init__(self)

        button = gtk.Button("Open database")
        button.connect("clicked", self.open_button_cb)
        button.show()
        label = gtk.Label("oml2-view")
        self.insert_page(button, label)

        self.open_database(datasource)

    def open_database(self, datasource=None):
        if datasource is None:
            chooser = gtk.FileChooserDialog(title="Select database to inspect",
                    action=gtk.FILE_CHOOSER_ACTION_OPEN,
                    buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OPEN, gtk.RESPONSE_OK))
            chooser.run()
            datasource = chooser.get_filename()
            chooser.destroy()
        ds = Oml2DataSourceSq3(datasource) 
        self.add_tabs(ds)

    def cleanup_tabs(self):
        for i in reversed(range(1, self.get_n_pages())):
            self.remove_page(i)

    def add_tabs(self, ds):
        self.cleanup_tabs()
        for table in ds.tables_list():
            label = gtk.Label(table)
            graph=Oml2GraphPane(ds, table)
            self.append_page(graph, label)
        if self.get_n_pages() > 1:
            self.set_current_page(1)

    def open_button_cb(self, data):
        self.open_database()
