#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
  struct stat st;
  int res = lstat("lzocomp.cc", &st);
  if(res != 0) {
    printf("Error %d\n", res);
    return 1;
  }
  printf("st_dev  = %lX\n", st.st_dev);
  printf("st_ino  = %lX\n", st.st_ino);
  printf("st_mode = %o\n", st.st_mode);
  printf("st_nlink = %lu\n", st.st_nlink);
  printf("st_uid   = %d\n", st.st_uid);
  printf("st_gid   = %d\n", st.st_gid);
  printf("st_rdev  = %lu\n", st.st_rdev);
  printf("st_size  = %lu\n", st.st_size);
  printf("st_blksize = %lu\n", st.st_blksize);
  printf("st_blocks  = %lu\n", st.st_blocks);
  printf("st_atime   = %lX\n", st.st_atime);
  printf("st_mtime   = %lX\n", st.st_mtime);
  printf("st_ctime   = %lX\n", st.st_ctime);
  return 0;
}
