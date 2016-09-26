# README


This application is a user space file system that is implemented on top
of the fuse (FILE SYSTEM IN USER SPACE) library. The file system implements
3 kinds of caching mechanism namely 

##### 1) Path translation Caching
##### 2) Directory Entry Calching
##### 3) Write Back Caching

All caches implement the LRU algorithm for cache eviction, two pools of 
dirty and clean pages are maintained for the Write Back Cache.


# Testing:
--------

The file System, has been tested and the tests cases are enclosed in 
this delivery, the tests can be run by using the phony "test" from the 
Makefile. 
Thus to understand the tests and test any modifications in the file 
system the folllwing commands can be used.
						
*make test*


# Code Coverage:

During our testing, roughly 89% of the code was found to be covered.
We have extensively tested the code for coverage,
except for error cases when reading and writing to disk
which are difficult to replicate in a test system.

To check the code coverage of the file system one must use the following
series of steps.

*make clean; make veryclean;*
	
On a separate terminal run
*./homework -d -s -image <fs image name> [-part <2 or 3 or 4> ]*
	
	(The number after part denotes the level of caching currently 
	being used in the system)

*make test*

					
# Read/Write comparisons with caching:

The script trace1.sh can be used to trace the number of read and write 
operations made by the File system. The tracescript can be executed as 
shown blow:

*./trace1.sh <Sm Directory inside the FS>*
			
The trace script generates log files in the /tmp 
folder which can also be analyzed using the collapse.py script
to compare the difference in the number of reads and write 
between this FS and any general purpose FS.
