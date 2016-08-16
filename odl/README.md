OpenDalyight utilities
======================

Various utilities to control an OpenFlow DAQ network (parallel leaf spine with daqrings) with the OpenDaylight controller.

For detailed description see the following research paper:

"A Lossless Network for Data Acquisition" - https://cds.cern.ch/record/2162697

Installation
------------
Apply patch for your OpenDaylight release, which adds support for the daqring ports (see OVS).

Put the odl_utils Python package into your Python path.

Tested with Python 2.7. Installation of additional packages can be required.

Usage
-----
Look at the various examples how to use the odl_utils package.
