#! /bin/sh

rm -f dict/*

for word in `cat dict.txt`
do
	printf "$word" >dict/$word;
done
