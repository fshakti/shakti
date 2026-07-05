#include "shakti.h"

#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <errno.h>
#include <string.h>

#if defined(_WIN32)
V*subprocess(V**a,in){
  return v_err("nyi");
}
#else // linux, apple, etc
V*subprocess(V**a,in){
  extern char **environ;
  struct stat sb;
	if(stat("run",&sb))return NULL;

#if defined(__linux__)
	int t;
  do t=open("/dev/ptmx", O_RDWR|O_NOCTTY); while (t<0&&(errno==ENOSPC||errno==EAGAIN));
  if(t<0)return v_err("/dev/ptmx");

  int p = 0; ioctl(t,TIOCSPTLCK,&p); if(ioctl(t,TIOCGPTN,&p)) return close(t),v_err("TIOCGPTN");
	char pts[20]; snprintf(pts,sizeof(pts)-1,"/dev/pts/%d",p);
#else // apple, etc
  int t = posix_openpt(O_RDWR|O_NOCTTY);
  grantpt(t);unlockpt(t);
  char pts[1024];
  if(ptsname_r(t,pts,sizeof(pts)-1)!=0)return close(t),v_err("ptsname_r");
#endif

 // todo cgroups etc
  pid_t pid;
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  posix_spawn_file_actions_addopen(&file_actions, 0, pts, O_RDWR, 0666);
  posix_spawn_file_actions_addopen(&file_actions, 1, pts, O_WRONLY, 0666);
  posix_spawn_file_actions_addopen(&file_actions, 2, pts, O_WRONLY, 0666);
	const char *argv[] = { "./run", 0 };
	int r = posix_spawn(&pid, *argv, &file_actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&file_actions);
	if(r) return close(t),v_err("subprocess");
  fcntl(t, F_SETFL, fcntl(t, F_GETFL, 0) | O_NONBLOCK);
	return v_subprocess(t, pid);
}
V *subprocess_next(V*p, double to) {
	unsigned char buffer[16536];
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(p->j,&rfds);
	struct timeval tv={.tv_sec=to/1000.0};
	tv.tv_usec = ((int64_t)((to-(double)(int64_t)to/1000)*1000.0))%1000000;
	while(select(p->j+1, &rfds,NULL,NULL,&tv)==-1&&errno==EINTR);
	int r = read(p->j, buffer, sizeof(buffer)-1);
  if(r >= 0) {
    for(int i = 0; i < r; ++i) if(
      /* ascii control codes */ buffer[i]<7 || (buffer[i]>=14 && buffer[i]<=26)
      /* invalid utf8 sequences */ ||buffer[i]>=0xf5||buffer[i]==0xc0||buffer[i]==0xc1) {
      V*x = v_cvec(r);
      memcpy(x->B,buffer,r);
      return x;
    }
    buffer[r]=0;
    return v_str((char*)buffer);
  }
	return v_nil();
}
V *subprocess_status(V*p) {
	if(p->n > 1) {
    int status;
    if(waitpid(p->n, &status, WNOHANG)>0){
       p->n=WIFEXITED(status)?0:-1;
       p->b=WIFEXITED(status)?WEXITSTATUS(status):WTERMSIG(status);
    }
	}
	switch(p->n){
  case 0: return v_char(p->b);
  case -1: return v_int(-p->b);
	default: return v_nil(); /* no status (running) */
	}
}
int64_t subprocess_send(V*p,V**a,in) {
	struct iovec iov[n],*iovp=iov;
	for(int i=0;i<n;++i) {
    switch(a[i]->t) {
    case T_STR: iov[i].iov_len = strlen(iov[i].iov_base = a[i]->s);  break;
    case T_CVEC: iov[i].iov_base = a[i]->B; iov[i].iov_len = a[i]->n; break;
    default: iov[i].iov_base = ""; iov[i].iov_len = 0; break;
		};
	}
	int64_t s=0;
	while(n > 0) {
    ssize_t i,r = writev(p->j,iovp,n);
	  if(r > 0)for(s+=r, i=0;r>0&&i<n;)if(r >= iovp[i].iov_len)r -= iovp[i].iov_len, n--, iovp++;
			else if(r > 0) iovp[i].iov_base += r, iovp[i].iov_len -= r, r=0;
	}
	return s;
}

#endif
