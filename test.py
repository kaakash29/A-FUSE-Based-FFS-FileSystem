import os;
import commands;

testnumber = 0 
failedCase = 0
op = ""

def execute(command):
	"""
	Executes a given command
	"""
	return commands.getoutput(command)

def test_sizes(filename):
	"""
	Test the calculated sizes of a file
	"""
	global failedCase, testnumber, op
	
	sizesmall = execute("wc --bytes "+filename).split(" ")[0]
	lsizesmall = execute("ls -l "+filename).split(" ")[4]
	
	if (lsizesmall != sizesmall):
		print ("*****")
		print ("\n Test Number  " + str(testnumber) + "  Failed.")
		print ("\n Expected Size : "   + str(lsizesmall))
		print ("\n Received Size: "   + str(sizesmall))
		failedCase = failedCase + 1
		print ("******")
		
	testnumber = testnumber + 1
	return sizesmall


def check_file_size(filename, size):
	"""
	Check size of file against given length
	"""
	global failedCase, testnumber, op
	length = test_sizes("dir/file.A")
	
	if (length != size):
		print ("*****")
		print ("\n Test Number  " + str(testnumber) + "  Failed.")
		print ("\n Expected Size : "   + str(size))
		print ("\n Received Size: "   + str(length))
		failedCase = failedCase + 1
		print ("******")
	
	testnumber = testnumber + 1
	
	
def test(command, output):
	"""
	Tests the result of a command against a given output
	"""
	global failedCase, testnumber, op
	
	op = execute(command)
	if output not in op:
		print ("*****")
		print ("Test Number  " + str(testnumber) + "  Failed.")
		print ("\n Executed : "+ command)
		print ("\n Expected : " + output)
		print ("\n received : " + op)
		failedCase = failedCase + 1
		print ("******")
		
	testnumber = testnumber + 1
	return 1

def tests():
	"""
	executes the tests in a row 
	"""
	test("ls dir/file.A/file.0", "ls: cannot access") 							#0 
	test("ls dir/file.A/file.0", "ls: cannot access") 							#1
	test("ls dir/filebhjgjhgjgjhgjA","ls: cannot access") 						#2
	test("ls -l dir/file.A", "-rwxrwxrwx 0 student student 1000") 				#3
	test("ls -ld dir", "drwxrwxrwx 0 student student")		 					#4
	test("ls file.A/", "ls: cannot access") 									#5
	test("ls dir/", "dir1")														#6
	test("ls dir/", "file.A")													#7
	test("touch dir/FOO/file.A", "touch: cannot touch") 						#8
	test("touch dir/file.A/file.9", "touch: cannot touch ")						#9
	
	execute("mkdir dir/dir2")
	for i in range(1, 33):
		execute("mkdir dir/dir2/"+str(i))
		
	test("mkdir dir/dir2/extra","No space left on device")						#10
	
	execute("rm -r dir/dir2/1")
	test("mkdir dir/dir2/extra1","")											#11
	test("mkdir dir/FOO/file.A","mkdir: cannot create directory")				#12
	test("mkdir dir/file.A/dir","mkdir: cannot create directory")				#13
	test("mkdir dir/file.A", "mkdir: cannot create directory")					#14
	test("mkdir dir/dir1", "mkdir: cannot create directory")					#15
	test("rmdir dir/dir1/FOO", "rmdir: failed to remove")						#16
	test("rmdir dir/dir3","rmdir: failed to remove")							#17
	test("rmdir dir/file.A", "rmdir: failed to remove")							#18
	
	execute("mkdir dir/dir4; touch dir/dir4/file.txt")
	test("rmdir dir/dir4", "rmdir: failed to remove")							#19
	test("rmdir dir/dir2", "")													#20
	test("mv dir/dir5/file.A dir/dir4", "mv: cannot stat")						#21
	test("mv /dir5/foo.file  dir/dir4", "mv: cannot stat")						#22
	test("mv dir/file.A/file.0 dir/dir4", "mv: cannot stat")					#23
	test("mv dir/file.A dir/dir1/file.0", "")									#24
	test("mv dir/file.A dir/dir1/","")											#25
	test("chmod 777 dir/dir1/file.9", "chmod: cannot access")					#26
	test("chmod 777 dir/dir9/file.2", "chmod: cannot access")					#27
	
	execute("chmod 777 dir/dir1")
	test("ls -ld dir/file.A","-rwxrwxrwx 0")									#28
	test("touch dir/dir1/newfile.txt", "")										#29
	test("touch dir/dir9/newfile.txt", "touch: cannot touch")					#30
	test("touch dir/file.A/newfile.txt", "touch: cannot touch")					#31	
	
	old = execute("ls -ld dir/dir2")
	execute("touch dir/dir2")
	new = execute("ls -ld dir/dir2")
	
	if (old == new):															#32
		print "\n Only Touch test failed =) "
		
	
	execute("touch dir/smallfile.txt; touch dir/mediumfile.txt; touch dir/largefile.txt")
	
	execute("ls -l /usr/bin > dir/smallfile.txt")
	test_sizes("dir/smallfile.txt")												#33
	
	execute("ls -ltr /bin > dir/mediumfile.txt")
	test_sizes("dir/mediumfile.txt")											#34
	
	execute("ls -ltr /usr/share/* > dir/largefile.txt")
	test_sizes("dir/largefile.txt")												#35
	
	check_file_size("dir/file.A", str(len(execute("cat dir/file.A"))))			#36
	
	
	test("rm -f dir/file.C/file.A", "")						#37
	test("rm -f dir/file.A", "")												#38
	test("rm -f dir/dir1/", "rm: cannot remove")								#39
	test("rm -f dir/file.A", "")												#41
	test("rm -f dir/mediumfile.txt", "")										#42
	test("rm -f dir/largefile.txt", "")											#43
	test("trancate --size 0 dir/dir1/file.0", "")								#44
	test("truncate --size 0 dir/dir1/", "truncate: cannot open")				#45
	test("truncate --size 20 dir/file.A", "truncate: failed to truncate")		#47
	test("truncate --size 0 dir/file.A", "")
	

def start_test():
	global testnumber, failedCase
	tests()
	print ("\n-------------------------------------------------------\n")
	print ("\n Total tests executed = "+str(testnumber))
	print ("\n Total tests failed   = "+str(failedCase))
	print ("\n-------------------------------------------------------\n")
	
start_test()
