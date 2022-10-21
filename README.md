# ColdMUD, 1993-1994

This is an historical archive of the ColdMUD server -- a networked
multiuser persistent object oriented programming language -- written
by Greg Hudson from 1993 to 1994.

It is a variant of a type of MUD/MOO server similar to LambdaMOO.

The sources are based on archives I found https://stuff.mit.edu/afs/sipb.mit.edu/project/coldmud/ and currently represent the source from versions 0.8 to 0.11, preserved in the git history with commit messages derived from the original CHANGELOG.

I was unable to find any source archives earlier than `0.8`. The source trees I found after 0.11 (`dev-0.11`) take a bit of a right turn off into other directions, with many source files partially rewritten to C++. Those are captured in branches, rather than on master.

After the `0.11` (spring 1994) time period, other people took over development of ColdMUD, and it forked into two separate paths:

  * "Genesis" was a fork by Brandon Gillespie and others, and is preserved here: https://github.com/the-cold-dark/genesis
  * And a continuation of the original `coldmud` sources by Colin McCormack, which kept the coldmud name and version numbering system. I am currently sourcing these changes from Colin and will preserve them here.

Note: The documentation and tests here are taken from the `dev-0.11` tree, but the source is from `0.11-alpha`
