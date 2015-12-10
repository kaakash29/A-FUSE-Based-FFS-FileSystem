#!/bin/bash

##########################################################
# This is a test script for checking the various aspects #
# of our file system                                     #
##########################################################

#---------------------------------------
# This is the area for general Function 
#---------------------------------------

function fail() {
# The shell function to exit on failure 
# after printing a message
echo ""
echo "************************* "
echo $*
echo "************************* "
echo ""
exit
}
# ---------------------------------------------------------



# 1. Path translation error:
# a. Middle of path is a file 

test "$(ls dir/file.A/file.0)"=="ls: cannot access dir/file.A/file.0: Not a directory" || fail "TEST 1a FAILED"

# b. Middle of path is BAD

test "$(ls dir/  /file.A)"=="ls: cannot access /file.A: No such file or directory" || fail "TEST 1b FAILED"

# c. End of the path is BAD

test "$(ls dir/filebhjgjhgjgjhgjA)"=="ls: cannot access dir/filebhjgjhgjgjhgjA: No such file or directory"||fail "TEST 1c FAILED"
# -----

# 2. Testing getattr :
# a. getattr for a file

test "$(ls -l dir/file.A)"=="-rwxrwxrwx 0 student student 1000 Jul 13  2012 dir/file.A" || fail "Test 2a Failed"

# b. getattr for a directory

test "$(ls -ld dir)"=="drwxrwxrwx 0 student student 1024 Jul 13  2012 dir" || fail "Test 2b Failed"
# -----

# 3. Testing readdir :
# a. not a directory

test "$(ls file.A/)"=="ls: cannot access file.A/: No such file or directory" || fail "Test 3a Failed"

# b. a directory only valid entries are returned

test "$(ls dir/)"=="dir1  file.A" || fail "Test 3b.1 Failed"
test "$(ls dir/)"=="file.0  file.2" || fail "Test 3b.2 Failed"
# -----


# 4. Testing mknod :
# a. bad path   

test "$(touch dir/FOO/file.A)"=="touch: cannot touch ‘dir/FOO/file.A’: No such file or directory" || fail "Test 4a Failed"

# b. isnt directory

test "$(touch dir/file.A/file.9)"=="touch: cannot touch ‘dir/file.A/file.9’: Not a directory" || fail "Test 4b Failed"

# c. file exists

#test "$(touch dir/dir1/file.2)"

# d. directory exists 

#test "$(touch dir/dir1)"

# e. Directory full
mkdir dir/dir2

for i in {1..32}
do 
	mkdir -p dir/dir2/$i
done

test "$(mkdir dir/dir2/extra)"=="mkdir: cannot create directory ‘dir/dir2/extra’: No space left on devic" || fail "Test 4e Failed"

# f. directory full (2)

rm -r dir/dir2/1

test "$(mkdir dir/dir2/extra1)"==1 || fail "Test 4f Failed"

# 5. Testing mkdir

# a. bad path   

test "$(mkdir dir/FOO/file.A)"=="mkdir: cannot create directory ‘dir/FOO/file.A’: No such file or directory" || fail "Test 5a Failed"

# b. isnt directory

test "$(mkdir dir/file.A/dir)"=="mkdir: cannot create directory ‘dir/file.A/dir’: Not a directory" || fail "Test 5b Failed"

# c. mkdir filename exists

test "$(mkdir dir/file.A)"=="mkdir: cannot create directory ‘dir/file.A’: File exists" || fail "Test 5c Failed"

# d. mkdir directory exists

test "$(mkdir dir/dir1)"=="mkdir: cannot create directory ‘dir/dir1’: File exists" || fail "Test 5d Failed"

# e f 
# These tests are already 2written in the test case above change teh above test by replacing the 

# ------
# 6. Testing rmdir:
# bad path (1)

test "$(rmdir dir/dir1/FOO)"=="rmdir: failed to remove ‘dir/dir1/FOO’: No such file or directory" || fail "Test 6a failed"

# bad path (2)

test "$(rmdir dir/dir3)"=="rmdir: failed to remove ‘dir/dir3’: No such file or directory" || fail "test 6b failed"

# file doesnt exist

test "$(rmdir dir/FOO.txt)"=="rmdir: failed to remove ‘dir/FOO’: No such file or directory" || fail "Test 6c Failed"

# directory doesnt exist 

test "(rmdir dir/dir3)"=="rmdir: failed to remove ‘dir/dir3’: No such file or directory" || fail "Test 6d Failed"

# deleting a file with rmdir

test "$(rmdir dir/file.A)"=="rmdir: failed to remove ‘dir/file.A’: No such file or directory" || fail "Test 6e failed"

# tring to delete a directory that is not empty 

mkdir dir/dir4
touch dir/dir4/file.txt
test "$(rmdir dir/dir4)"=="rmdir: failed to remove ‘dir/dir4’: Directory not empty" || fail "Test 6f failed"

# good path 

test "$(rmdir dir/dir2)"==0 || fail "Test 6g Failed"

# -------

# 7. Testing rename
# a. bad path

test "$(mv dir/dir5/file.A dir/dir4)"=="mv: cannot stat ‘dir/dir5/file.A’: No such file or directory" || fail "Test 7a Failed"

# b. bad path c doesnt exist 

test "$(mv /dir5/foo.file  dir/dir4)"=="mv: cannot stat ‘/dir5/foo.file’: No such file or directory" || fail "Test 7b failed"

# c. bad path b is a file

test "$(mv dir/file.A/file.0 dir/dir4)"=="mv: cannot stat ‘dir/file.A/file.0’: Not a directory" || fail "Test 7c Failed"
 
# d. destination is already existant in a file

test "$(mv dir/file.A dir/dir1/file.0)"==0 || fail "Test 7d failed"

# e. destination is already existant in a directory 

test "$(mv dir/file.A dir/dir1/)"==0 || fail "Test 7e Failed"

# ------

# 8. testing chmod 
# a. bad path 
test "$(chmod 777 dir/dir1/file.9)"=="chmod: cannot access ‘dir/dir1/file.9’: No such file or directory" || fail "Test 8a.1 Failed"
test "$(chmod 777 dir/dir9/file.2)"=="chmod: cannot access ‘dir/dir9/file.2’: No such file or directory" || fail "Test 8a.1 Failed"

# b. for directories
chmod 777 dir/dir1
test "$(ls -ld dir/file.A)"=="-rw-r--r-- 0 student student 1000 Jul 13  2012 dir/file.A" || fail "Teast 8b Failde"

# c. for files 
chmod 644 dir/file.A
test "$(ls -ld dir/file.A)"=="-rw-r--r-- 0 student student 1000 Jul 13  2012 dir/file.A" || fail "Teast 8c Failde"

# -------

# 9. testing utime 
# a. bad paths
# i. If file non existant in path create path 
test "$(touch dir/dir1/newfile.txt)"==0 || fail "Test 9.a.1 Failed"

# ii. if intermediate directory non existant 
test "$(touch dir/dir9/newfile.txt)"=="touch: cannot touch ‘dir/dir9/newfile.txt’: No such file or directory" || fail "Test 9.a.1 Failed"

# iii. if path has a file in the middle 
test "$(touch dir/file.A/newfile.txt)"=="touch: cannot touch ‘touch: cannot touch ‘dir/file.A/newfile.txt’: No such file or directory" || fail "Test 9.a.3 Failed"

# b. for directories
old=$(ls -ld dir/dir2)
touch dir/dir2
test "ls -ld dir/dir2"!="$old" || fail "Test 9b failed"

# c. for files
oldfile=$(ls -ld dir/file.A)
touch dir/file.A
test "ls -ld dir/dir2"!="$oldfile" || fail "Test 9c failed"

# ---------
# 10. Testing read

size1=$(wc --bytes dir/file.A | cut -d" " -f1)
cof=$(cat dir/file.A)
l=`expr length $cof`

test $l==$size1 || fail "test 10 failed"

fail "ALL TESTS PASSED !! =)"
