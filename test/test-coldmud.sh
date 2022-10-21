#!/bin/sh

cp source textdump
$1 . 2> output
perl get-output.pl source > ideal-output
perl prune-output.pl output > actual-output
if cmp -s ideal-output actual-output; then
	echo "Test passes."
else
	echo "Test fails:"
	diff ideal-output actual-output
fi
rm -rf textdump binary output ideal-output actual-output

