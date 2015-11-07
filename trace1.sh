#!/bin/sh

# usage: trace1.sh <directory>
D=$1

# create <filename> <len>
create(){
    head --bytes $2 /dev/zero > $1
}
create2(){
    head --bytes $2 /dev/zero | dd bs=120 of=$1
}

mkdir $D/dir1/
create $D/dir1/file1     6387
create $D/dir1/file2    39524
create $D/dir1/file3     4033
create $D/dir1/file4      331
create $D/dir1/file5    14444
create $D/dir1/file6     9705
create $D/dir1/file7    40521
mkdir $D/dir2/
create $D/dir2/file1     177
create $D/dir2/file2    2302
create $D/dir2/file3     226
create $D/dir2/file4   39524
create $D/dir2/file5    4033
create $D/dir2/file6     331
create $D/dir2/file7   14444
create $D/dir2/file8    9705
create $D/dir2/file9   40521
create $D/dir2/file10    177
create $D/dir2/file11   2302
create $D/dir2/file12    226
create $D/dir2/file13   6387
create $D/dir2/file14  37810
mkdir $D/dir3/
create $D/dir3/file1   37705
create $D/dir3/file2     886
create $D/dir3/file3    1853
create $D/dir3/file4    3543
create $D/dir3/file5   26967
create $D/dir3/file6    2134
create $D/dir3/file7   29727
create $D/dir3/file8     464
create $D/dir3/file9     816
create $D/dir3/file10   1064
create $D/dir3/file11    319
create $D/dir3/file12  32021

cat $D/dir1/file1 > /dev/null
cat $D/dir1/file2 > /dev/null
cat $D/dir2/file1 > /dev/null
create2 $D/dir1/filex1    1064

cat $D/dir1/file4 > /dev/null
cat $D/dir2/file2 > /dev/null
cat $D/dir2/file3 > /dev/null
create2 $D/dir2/filex1     319

cat $D/dir3/* > /dev/null
create $D/dir3/filex1   32021

cat $D/dir1/file[3-8] > /dev/null
create2 $D/dir1/filex2    2427

rm -rf $D/dir3
create2 $D/dir2/filex2     365
create2 $D/dir1/filex3     583

rm $D/dir2/file5
rm $D/dir1/file6
rm $D/dir1/file8
rm $D/dir2/file[234]

