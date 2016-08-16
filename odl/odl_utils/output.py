#!/usr/bin/env python
# © Copyright 2016 CERN
#
# This software is distributed under the terms of the GNU General Public 
# Licence version 3 (GPL Version 3), copied verbatim in the file "LICENSE".
#
# In applying this licence, CERN does not waive the privileges and immunities
# granted to it by virtue of its status as an Intergovernmental Organization 
# or submit itself to any jurisdiction.
#
# Author: Grzegorz Jereczek <grzegorz.jereczek@cern.ch>
#
"""
Helpers to control the output.
(c) Copyright 2015 Grzegorz Jereczek. All Rights Reserved.
"""

class Bcolors:
    """Represents colors for different types of output messages."""
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class Output(object):
    """Represents an Output object to print messages of various types to the console."""

    def __init__(self, silent = False):
        """Creates an new Output object.

        Args:
            silent: If false, suppresses all the output.
        """
        self.__silent = silent

    def header(self, m):
        if not self.__silent:
            print Bcolors.HEADER + m + Bcolors.ENDC

    def okblue(self, m):
        if not self.__silent:
            print Bcolors.OKBLUE + m + Bcolors.ENDC

    def okgreen(self, m):
        if not self.__silent:
            print Bcolors.OKGREEN + m + Bcolors.ENDC

    def warn(self, m):
        if not self.__silent:
            print Bcolors.WARNING + m + Bcolors.ENDC

    def fail(self, m):
        if not self.__silent:
            print Bcolors.FAIL + m + Bcolors.ENDC

    def bold(self, m):
        if not self.__silent:
            print Bcolors.BOLD + m + Bcolors.ENDC

    def underline(self, m):
        if not self.__silent:
            print Bcolors.UNDERLINE + m + Bcolors.ENDC

    def normal(self, m):
        if not self.__silent:
            print m


