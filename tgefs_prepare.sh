#!/bin/bash
#
# Preparation script for TGE file system
#
#$ -S /bin/bash
#$ -v PATH
#$ -v HOME
#$ -v LD_LIBRARY_PATH
#$ -v USER
#$ -v PERL5LIB
#
fusermount -u tgefs
tgefs tgefs

