#include <stdio.h>
#include <unistd.h>
#include <sys/vfs.h>

int main()
{
  struct statfs sfs;
  const int res = statfs("/grid2/mkasa/tgetmp", &sfs);
  if(res != 0) {
    printf("Error %d\n", res);
    return 1;
  }
  printf("f_type   = %lX\n", sfs.f_type);
  printf("f_bsize  = %ld\n", sfs.f_bsize);
  printf("f_blocks = %ld\n", sfs.f_blocks);
  printf("f_bfree  = %ld\n", sfs.f_bfree);
  printf("f_bavail = %ld\n", sfs.f_bavail);
  printf("f_files  = %ld\n", sfs.f_files);
  printf("f_ffree  = %ld\n", sfs.f_ffree);
  //  printf("f_fsid   = %???\n", sfs.f_fsid);
  printf("f_namelen= %ld\n", sfs.f_namelen);
  return 0;
}
