#!/usr/bin/python

fn = raw_input('Enter file to collapse: ')

outputfile = fn.split("/")[-1].split(".")[0] + "-collapsed.txt"

f = open(fn, 'r')

outp = open(outputfile, 'w')

lines = f.readlines()
readcount = 0
writecount = 0
prevpart = " "

for lindex in range(2, len(lines)): 
	parts = lines[lindex].split(" ")	
	
	if parts[0] == "read":
		readcount = readcount + 1
	
	if parts[0] == "write":
		writecount = writecount + 1

	if (parts[0] == prevpart):
		continue
	else:
		outp.write(str(prevpart)+"\n")
	
	prevpart = parts[0]
	

outp.write(str(prevpart)+"\n")
outp.close()

outp2 = open(outputfile, "r+")

lines = outp2.readlines()

newreadcount = 0
newwritecount = 0

for line in lines: 
	if "read" in line:
		newreadcount = newreadcount + 1
	
	if "write" in line:
		newwritecount = newwritecount + 1


outp2.write("Number of OLD read and write operations = " + str(readcount) + " reads  and " + str(writecount) + " writes\n")	
outp2.write("Number of NEW read and write operations = " + str(newreadcount) + " reads  and " + str(newwritecount) + " writes \n")
	
	
