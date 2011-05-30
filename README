====================================
TGEFS (Tiny Grid Engine File System)
====================================

Installation
============

To install, do the following::

    ./configure
    make
    sudo make install


How to use
==========

Please look the FUSE documentation for basic usage of FUSE-based
file system. When you specify a mount point /X, then a file /Y/Z
can be accessed by the path /X/Y/Z. For example, if '/home/foo'
is a remote directory via NFS and you mount TGEFS at '/home/foo/tgefs',
you can access to '/home/foo/bar/test.fasta' by accessing to
'/home/foo/tgefs/home/foo/bar/test.fasta'.

To specify cache directory, create '~/.tge/.tgerc' and write like this::

	tgelocaldisk=/path/to/cache/dir

TGEFS can compress files in the remote directory. lzo/unlzo are
utilities for LZO compression and decompression. lcat is zcat equivalent
for LZO compression.

There are files suitable for compression and files not suitable for
compression. The former includes text files, while the latter includes
jpeg files and already compressed files such as .gz or .bz2 files.
In addition, you may want not to compress some files such as dot files
in your home directory. To control which files are to be compressed,
you can specify a condition in ~/.tge/.tgefs. Please look the comment
in tge_compctl.cc for this feature.


How it works
============

When a open syscall is issued by a user program, a possibly remote
file is copied into the cache directory, which is then opened and
passed to the user program. If any write is observed to the opened
file, the modified file is written back to the remote directory
automatically.

Cached files are removed if the free disk space becomes less than
30% of the total size of the disk on which the cache directory
resides. Cache files more than 14-day-old are removed because they
are unlikely to be used in the near future. To avoid performance
problem due to too many (possibly small) files, up to 1000 files
are cached, and older files will be removed to cache a new one.

LZO-compressed files are decompressed when they are copied into
the cache directory. Compression is done when cache files are
written back to their original location. 


Tips
====

boot.tgefs is a startup script for RedHat-flavored Linux.

SSD is suitable for cache directory.

You can change several parameters by changing constants in
tge_appconfig.cc.


Note
====

We implemented a lock mechanism to avoid concurrent access to
the remote file server, but it did not work well for reasons.
Please do not use the lock mechanism.

Tiny Grid Engine is a lightweight batch-job system for small Linux
clusters, but it is not distributed. Don't search it. Tiny Cloud
Engine project took over the project, and has additional features
suitable for developing complex software on relatively small clusters.


License
=======

TGEFS is licensed under GNU GPL ver 2.1

A part of this work is derived from fusexmp.c in FUSE distribution,
which was developed by Miklos Szeredi <miklos@szeredi.hu>.

LZO compression code is from minilzo, which was developed by
Markus F.X.J. Oberhumer.
