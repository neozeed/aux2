#ifndef lint	/* .../sys/COMMON/os/sysent.c */
#define _AC_NAME sysent_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 1.13 90/01/13 13:53:57}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.13 of sysent.c on 90/01/13 13:53:57";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/* @(#)sysent.c	1.3 */

#include "sys/types.h"
#include "sys/sysent.h"

/*
 * This table is the switch used to transfer
 * to the appropriate routine for processing a system call.
 */

int	errsys();
int	netdown();
int	nosys();
int	nullsys();

int	access();
int	adjtime();
int	alarm();
#define	async_daemon	nosys
int	chdir();
int	chmod();
int	chown();
int	chroot();
int	close();
int	creat();
int	dup();
int	exec();
int	exece();
#define	exportfs	nosys
int	fchmod();
int	fchown();
int	fcntl();
int	flock();
int	fork();
int	fstat();
int	fstatfs();
int	fsync();
int	ftruncate();
int	getcompat();
int	getdirentries();
int	getdomainname();
int	getdtablesize();
int	getgid();
int	getgroups();
int	getitimer();
int	getpid();
int	gettimeofday();
int	getuid();
int	gtime();
int	ioctl();
int	kill();
int	link();
int	lock();
int	lstat();
int	mkdir();
int	mknod();
int	mount();
int	msgsys();
#define	nfs_getfh	nosys
#define	nfs_svc		nosys
int	nice();
int	ofstat();
int	open();
int	ostat();
int	pause();
int	pipe();
int	profil();
int	ptrace();
#ifdef	QUOTA
int	quotactl();
#else
#define	quotactl	errsys
#endif
int	read();
int	readv();
int	readlink();
int	rename();
int	rexit();
int	rmdir();
int	sbreak();
int	seek();
int	semsys();
int	setcompat();
int	setdomainname();
int	setgid();
int	setgroups();
int	setitimer();
int	setpgrp();
int	setregid();
int	setreuid();
int	settimeofday();
int	setuid();
int	shmsys();
int	sigblock();
int 	sigcleanup();
int	sigpause();
#ifdef POSIX
int	sigpending();
int	setsid();
int	setpgid();
int	getcterm();
int     waitpid();
#endif  /* POSIX */
int	sigsetmask();
int	sigstack();
int	sigvec();
int	ssig();
int	stat();
int	statfs();
int	stime();
int	symlink();
int	sync();
int	sysacct();
#if	defined(m68k)
int	sysm68k();
#else
#define	sysm68k	nosys
#endif
int	times();
int	truncate();
int	ulimit();
int	umask();
int	umount();
int	unlink();
int	unmount();
int	utime();
int	utimes();
int	utssys();
#ifdef	TRACE
int	vtrace();
#else
#define	vtrace	nosys
#endif	TRACE
int	wait();
int	write();
int	writev();

/* net stuff */
int	gethostid();
int	gethostname();
int	sethostid();
int	sethostname();
int 	select();

#define	accept		netdown
#define	bind		netdown
#define	connect		netdown
#define	getpeername	netdown
#define	getsockname	netdown
#define	getsockopt	netdown
#define	listen		netdown
#define	recv		netdown
#define	recvfrom	netdown
#define	recvmsg		netdown
#define	send		netdown
#define	sendmsg		netdown
#define	sendto		netdown
#define	setsockopt	netdown
#define	shutdown	netdown
#define	socket		netdown
#define	socketpair	netdown

/*
 * Local system calls
 */
int	locking();
int	phys();
int	reboot();
int	powerdown();
int	sysslotmanager();
int	swapmmumode();

struct sysent sysent[] =
{
	0, 0, nosys,			/*  0 = indir */
	1, 0, rexit,			/*  1 = exit */
	0, 0, fork,			/*  2 = fork */
	3, 1, read,			/*  3 = read */
	3, 1, write,			/*  4 = write */
	3, 1, open,			/*  5 = open */
	1, 1, close,			/*  6 = close */
	1, 1, wait,			/*  7 = wait */
	2, 1, creat,			/*  8 = creat */
	2, 0, link,			/*  9 = link */
	1, 0, unlink,			/* 10 = unlink */
	2, 0, exec,			/* 11 = exec */
	1, 0, chdir,			/* 12 = chdir */
	1, 0, gtime,			/* 13 = time */
	3, 0, mknod,			/* 14 = mknod */
	2, 0, chmod,			/* 15 = chmod */
	3, 0, chown,			/* 16 = chown; now 3 args */
	1, 0, sbreak,			/* 17 = break */
	2, 0, ostat,			/* 18 = ostat */
	3, 0, seek,			/* 19 = seek */
	0, 0, getpid,			/* 20 = getpid */
	0, 0, nosys,			/* 21 = old svfs_mount */
	1, 0, unmount,			/* 22 = unmount */
	1, 0, setuid,			/* 23 = setuid */
	0, 0, getuid,			/* 24 = getuid */
	1, 0, stime,			/* 25 = stime */
	4, 0, ptrace,			/* 26 = ptrace */
	1, 0, alarm,			/* 27 = alarm */
	2, 0, ofstat,			/* 28 = ofstat */
	0, 1, pause,			/* 29 = pause */
	2, 0, utime,			/* 30 = utime */
	2, 0, nosys,			/* 31 = nosys */
	2, 0, nosys,			/* 32 = nosys */
	2, 0, access,			/* 33 = access */
	1, 0, nice,			/* 34 = nice */
	0, 0, nosys,			/* 35 = sleep; inoperative */
	0, 1, sync,			/* 36 = sync */
	2, 0, kill,			/* 37 = kill */
	5, 0, sysm68k,			/* 38 = m68k/3B specific */
	3, 0, setpgrp,			/* 39 = setpgrp */
	0, 0, nosys,			/* 40 = tell - obsolete */
	1, 0, dup,			/* 41 = dup */
	0, 1, pipe,			/* 42 = pipe */
	1, 0, times,			/* 43 = times */
	4, 0, profil,			/* 44 = prof */
	1, 0, lock,			/* 45 = proc lock */
	1, 0, setgid,			/* 46 = setgid */
	0, 0, getgid,			/* 47 = getgid */
	2, 0, ssig,			/* 48 = sig */
	6, 1, msgsys,			/* 49 = msg queue entry point */
	5, 1, sysm68k,			/* 50 = m68k/3B specific */
	1, 0, sysacct,			/* 51 = turn acct off/on */
	4, 1, shmsys,			/* 52 = shared memory */
	5, 1, semsys,			/* 53 = semaphore entry point */
	3, 1, ioctl,			/* 54 = ioctl */
	4, 0, phys,			/* 55 = phys */
	3, 0, locking,			/* 56 = file locking */
	3, 0, utssys,			/* 57 = utssys */
	0, 0, nosys,			/* 58 = reserved for USG */
	3, 0, exece,			/* 59 = exece */
	1, 0, umask,			/* 60 = umask */
	1, 0, chroot,			/* 61 = chroot */
	3, 1, fcntl,			/* 62 = fcntl */
	2, 0, ulimit,			/* 63 = ulimit */
	1, 1, reboot,			/* 64 = reboot */
	1, 1, powerdown,		/* 65 = x */
	2, 1, sysslotmanager,		/* 66 = slotmanager */
	1, 1, swapmmumode,		/* 67 = swapmmumode */
	0, 0, nosys,			/* 68 = x */
	0, 0, nosys,			/* 69 = x */
	3, 1, accept,			/* 70 = accept */
	3, 1, bind,			/* 71 = bind */
	3, 1, connect,			/* 72 = connect */
	0, 0, gethostid,		/* 73 = gethostid */
	2, 0, gethostname,		/* 74 = gethostname */
	3, 0, getpeername,		/* 75 = getpeername */
	3, 1, getsockname,		/* 76 = getsockname */
	5, 1, getsockopt,		/* 77 = getsockopt */
	2, 1, listen,			/* 78 = listen */
	4, 1, recv,			/* 79 = recv */
	6, 1, recvfrom,			/* 80 = recvfrom */
	3, 1, recvmsg,			/* 81 = recvmsg */
	5, 1, select,			/* 82 = select */
	4, 1, send,			/* 83 = send */
	3, 1, sendmsg,			/* 84 = sendmsg */
	6, 1, sendto,			/* 85 = sendto */
	1, 0, sethostid,		/* 86 = sethostid */
	2, 0, sethostname,		/* 87 = sethostname */
	2, 0, setregid,			/* 88 = setregid */
	2, 0, setreuid,			/* 89 = setreuid */
	5, 1, setsockopt,		/* 90 = setsockopt */
	2, 1, shutdown,			/* 91 = shutdown */
	3, 1, socket,			/* 92 = socket */
	4, 1, socketpair,		/* 93 = socketpair */
	0, 0, nosys,			/* 94 = nosys */
	0, 0, nosys,			/* 95 = x */
	0, 0, nosys,			/* 96 = x */
	0, 0, nosys,			/* 97 = x */
	0, 0, nosys,			/* 98 = x */
	0, 0, nosys,			/* 99 = x */
	2, 0, getdomainname,		/* 100 = getdomainname */
	2, 0, setdomainname,		/* 101 = setdomainname */
	2, 0, getgroups,		/* 102 = getgroups */
	2, 0, setgroups,		/* 103 = setgroups */
	0, 1, getdtablesize,		/* 104 = getdtablesize */
	2, 1, flock,			/* 105 = flock */
	3, 1, readv,			/* 106 = readv */
	3, 1, writev,			/* 107 = writev */
	2, 1, mkdir,			/* 108 = mkdir */
	1, 1, rmdir,			/* 109 = rmdir */
	4, 1, getdirentries,		/* 110 = getdirentries */
	2, 1, lstat,			/* 111 = lstat */
	2, 1, symlink,			/* 112 = symlink */
	3, 1, readlink,			/* 113 = readlink */
	2, 1, truncate,			/* 114 = truncate */
	2, 1, ftruncate,		/* 115 = ftruncate */
	1, 1, fsync,			/* 116 = fsync */
	2, 1, statfs,			/* 117 = statfs */
	2, 1, fstatfs,			/* 118 = fstatfs */
	0, 1, async_daemon,		/* 119 = async_daemon */
	0, 0, nosys,			/* 120 = old nfs_mount */
	1, 1, nfs_svc,			/* 121 = nfs_svc */
	2, 1, nfs_getfh,		/* 122 = nfs_getfh */
	2, 1, rename,			/* 123 = rename */
	2, 1, fstat,			/* 124 = fstat */
	2, 1, stat,			/* 125 = stat */
	2, 1, vtrace,			/* 126 = vtrace */
	0, 0, getcompat,		/* 127 = getcompat */
	1, 0, setcompat,		/* 128 = setcompat */
	3, 1, sigvec,			/* 129 = sigvec */
	1, 1, sigblock,			/* 130 = sigblock */
	1, 1, sigsetmask,		/* 131 = sigsetmask */
	1, 1, sigpause,			/* 132 = sigpause */
	2, 1, sigstack,			/* 133 = sigstack */
	2, 0, getitimer,		/* 134 = getitimer */
	3, 0, setitimer,		/* 135 = setitimer */
	1, 0, gettimeofday,		/* 136 = gettimeofday */
	1, 0, settimeofday,		/* 137 = settimeofday */
	2, 1, adjtime,			/* 138 = adjtime */
	4, 1, quotactl,			/* 139 = quotactl */
	3, 1, exportfs,			/* 140 = exportfs */
	4, 1, mount,			/* 141 = mount */
	1, 1, umount,			/* 142 = umount */
	3, 1, fchmod,			/* 143 = fchmod */
	3, 1, fchown,			/* 144 = fchown */
	2, 1, utimes,			/* 145 = utimes */
#ifdef POSIX
	0, 1, setsid,			/* 146 = setsid */
	2, 1, setpgid,			/* 147 = setpgid */
	0, 1, getcterm,			/* 148 = getcterm */
	1, 1, sigpending,		/* 149 = sigpending */
#else
	0, 0, nosys,			/* 146 = x */
	0, 0, nosys,			/* 147 = x */
	0, 0, nosys,			/* 148 = x */
	0, 0, nosys,			/* 149 = x */
#endif
 	0, 0, sigcleanup,		/* 150 = sigcleanup - hardwired trap.c */
#ifdef POSIX
	3, 1, waitpid,			/* 151 = waitpid */
#else /* !POSIX */
	0, 0, nosys,			/* 151 = x */
#endif /* POSIX */
	0, 0, nosys,			/* 152 = x */
	0, 0, nosys,			/* 153 = x */
	0, 0, nosys,			/* 154 = x */
	0, 0, nosys,			/* 155 = x */
	0, 0, nosys,			/* 156 = x */
	0, 0, nosys,			/* 157 = x */
	0, 0, nosys,			/* 158 = x */
	0, 0, nosys,			/* 159 = x */
	0, 0, nosys,			/* 160 = x */
	0, 0, nosys,			/* 161 = x */
	0, 0, nosys,			/* 162 = x */
	0, 0, nosys,			/* 163 = x */
	0, 0, nosys,			/* 164 = x */
	0, 0, nosys,			/* 165 = x */
	0, 0, nosys,			/* 166 = x */
	0, 0, nosys,			/* 167 = x */
	0, 0, nosys,			/* 168 = x */
	0, 0, nosys,			/* 169 = x */
	0, 0, nosys,			/* 170 = x */
	0, 0, nosys,			/* 171 = x */
	0, 0, nosys,			/* 172 = x */
	0, 0, nosys,			/* 173 = x */
	0, 0, nosys,			/* 174 = x */
	0, 0, nosys,			/* 175 = x */
	0, 0, nosys,			/* 176 = x */
	0, 0, nosys,			/* 177 = x */
	0, 0, nosys,			/* 178 = x */
	0, 0, nosys,			/* 179 = x */
	0, 0, nosys,			/* 180 = x */
	0, 0, nosys,			/* 181 = x */
	0, 0, nosys,			/* 182 = x */
	0, 0, nosys,			/* 183 = x */
	0, 0, nosys,			/* 184 = x */
	0, 0, nosys,			/* 185 = x */
	0, 0, nosys,			/* 186 = x */
	0, 0, nosys,			/* 187 = x */
	0, 0, nosys,			/* 188 = x */
	0, 0, nosys,			/* 189 = x */
	0, 0, nosys,			/* 190 = x */
	0, 0, nosys,			/* 191 = x */
	0, 0, nosys,			/* 192 = x */
	0, 0, nosys,			/* 193 = x */
	0, 0, nosys,			/* 194 = x */
	0, 0, nosys,			/* 195 = x */
	0, 0, nosys,			/* 196 = x */
	0, 0, nosys,			/* 197 = x */
	0, 0, nosys,			/* 198 = x */
	0, 0, nosys,			/* 199 = x */
	0, 0, nosys,			/* 200 = x */
	0, 0, nosys,			/* 201 = x */
	0, 0, nosys,			/* 202 = x */
	0, 0, nosys,			/* 203 = x */
	0, 0, nosys,			/* 204 = x */
	0, 0, nosys,			/* 205 = x */
	0, 0, nosys,			/* 206 = x */
	0, 0, nosys,			/* 207 = x */
	0, 0, nosys,			/* 208 = x */
	0, 0, nosys,			/* 209 = x */
	0, 0, nosys,			/* 210 = x */
	0, 0, nosys,			/* 211 = x */
	0, 0, nosys,			/* 212 = x */
	0, 0, nosys,			/* 213 = x */
	0, 0, nosys,			/* 214 = x */
	0, 0, nosys,			/* 215 = x */
	0, 0, nosys,			/* 216 = x */
	0, 0, nosys,			/* 217 = x */
	0, 0, nosys,			/* 218 = x */
	0, 0, nosys,			/* 219 = x */
	0, 0, nosys,			/* 220 = x */
	0, 0, nosys,			/* 221 = x */
	0, 0, nosys,			/* 222 = x */
	0, 0, nosys,			/* 223 = x */
	0, 0, nosys,			/* 224 = x */
	0, 0, nosys,			/* 225 = x */
	0, 0, nosys,			/* 226 = x */
	0, 0, nosys,			/* 227 = x */
	0, 0, nosys,			/* 228 = x */
	0, 0, nosys,			/* 229 = x */
	0, 0, nosys,			/* 230 = x */
	0, 0, nosys,			/* 231 = x */
	0, 0, nosys,			/* 232 = x */
	0, 0, nosys,			/* 233 = x */
	0, 0, nosys,			/* 234 = x */
	0, 0, nosys,			/* 235 = x */
	0, 0, nosys,			/* 236 = x */
	0, 0, nosys,			/* 237 = x */
	0, 0, nosys,			/* 238 = x */
	0, 0, nosys,			/* 239 = x */
	0, 0, nosys,			/* 240 = reserved for OEMs */
	0, 0, nosys,			/* 241 = reserved for OEMs */
	0, 0, nosys,			/* 242 = reserved for OEMs */
	0, 0, nosys,			/* 243 = reserved for OEMs */
	0, 0, nosys,			/* 244 = reserved for OEMs */
	0, 0, nosys,			/* 245 = reserved for OEMs */
	0, 0, nosys,			/* 246 = reserved for OEMs */
	0, 0, nosys,			/* 247 = reserved for OEMs */
	0, 0, nosys,			/* 248 = reserved for OEMs */
	0, 0, nosys,			/* 249 = reserved for OEMs */
	0, 0, nosys,			/* 250 = reserved for OEMs */
	0, 0, nosys,			/* 251 = reserved for OEMs */
	0, 0, nosys,			/* 252 = reserved for OEMs */
	0, 0, nosys,			/* 253 = reserved for OEMs */
	0, 0, nosys,			/* 254 = reserved for OEMs */
	0, 0, nosys,			/* 255 = reserved for OEMs */
};
int	nsysent = sizeof(sysent) / sizeof(struct sysent);

/* <@(#)sysent.c	6.2> */
