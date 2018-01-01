#!/usr/bin/python

# commandline uploader for LibXSVF-ESP
# example usage:
# ./svfupload.py 192.168.4.1 bitstream.svf

import urllib, urllib2, os, sys, time, curses
from poster.encode import multipart_encode
from poster.streaminghttp import register_openers

# Class used to display a progress bar in the console.
# Probably good only on Mac or Unix style OS's.
# From StackOverflow 5925028, 6169217, et al.
class Progress(object):
    def __init__(self, title):
        # Some initialization of variables
        self._seen=0.0;
        self._title = title;
        self._progress = 0.0;
        self._numberOfBars = 40
        self._numberOfBarsDisplayed = 0
        
        #print the initial progress meter
        sys.stdout.write(self._title + " [" + "-"* self._numberOfBars +"] %.2f%%" % (self._progress * 100.0))
        sys.stdout.flush();

    def update(self, total, size, args = 0):
        # Calculate the current progress
        self._seen += size;
        newProgress = self._seen/total;
        newBars = int(newProgress * self._numberOfBars)
        
        # Check to see if we need to add another hashmark to the progress meter
        if (self._numberOfBarsDisplayed < newBars):
            #If so, move the cursor back to the begninning of the meter and print it.
            lenOfCurrentProgress = len("%.2f%%" % (self._progress * 100.0))
            sys.stdout.write("\x1b[%dD" % lenOfCurrentProgress) #move back by the # of chars in %            
            sys.stdout.write("\x1b[%dD" % (2 + self._numberOfBars))
            sys.stdout.write("#" * newBars)
            
            #Move the cursor forward to the end of the progress meter and print the percentage
            sys.stdout.write("\x1b[%dC" % ((self._numberOfBars - newBars) + 2))
            newProgressLabel = "%.2f%%" % (newProgress * 100.0)
            sys.stdout.write(newProgressLabel)
            
            #Upadate the state for the next time
            self._progress = newProgress
            self._numberOfBarsDisplayed = newBars
            
        # Check to see if only the percentage has changed
        elif (newProgress > self._progress):
            #If so, move the cursor back to the beginning of the percentage area and print
            sys.stdout.write("\x1b[%dD" % len("%.2f%%" % (self._progress*100))) #move back by the # of chars in %
            sys.stdout.write("%.2f%%" % (newProgress * 100.0))
            self._progress = newProgress
        sys.stdout.flush();
            
# A file class to track the progress of the read
class file_with_callback(file):
    def __init__(self, path, mode, callback, *args):
        file.__init__(self, path, mode)
        self.seek (0, os.SEEK_END);
        self._total = self.tell();
        self.seek(0);
        self._callback = callback
        self._args = args
        
    def __len__(self):
        return self._total
        
    def read(self, size):
        data = file.read(self, size);
        self._callback(self._total, len(data), *self._args)
        return data
    
# This function uploads the file to 
def upload(path, sdCardIP, directory="/"):
    # Get the filename from the path and create the progress indicator
    filename = os.path.basename(path)
    progress = Progress(filename)
    # Open the file,  and set the callback to the progress indicator
    file = file_with_callback(path, 'rb', progress.update, path)
    # Upload the file using "poster" module
    register_openers()
    url = "http://%s" % (sdCardIP)
    values = { 'file':file }
    data, headers = multipart_encode(values)
    headers['User-Agent'] = 'Mozilla/4.0 (compatible; MSIE 5.5; Windows NT)'
    request = urllib2.Request(url, data, headers)
    # time.sleep(1)
    response = urllib2.urlopen(request)
    the_page = response.read();
    return the_page;
            
def print_usage():
    print "svfuploader.py <Hostname_or_IP> <filename>"
    
#MAIN SECTION

if len(sys.argv) != 3:
    print_usage()
    sys.exit(1)

cardip = sys.argv[1]
path = sys.argv[2]

if True:
  if (not os.path.exists(path)): 
    print "File not found: " + path;
    sys.exit(1)
  result = upload(path, cardip)
  if ("Done" in result):
    sys.stdout.write(" DONE\n")
  else:
    sys.stdout.write(" ERROR\n")
  print(result)
