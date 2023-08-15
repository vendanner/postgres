/*-------------------------------------------------------------------------
 *
 * pqsignal.c
 *	  Backend signal(2) support (see also src/port/pqsignal.c)
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/libpq/pqsignal.c
 *      信号名称	缺省动作	说明
 *	  1	SIGHUP	终止	终止控制终端或进程
      2	SIGINT	终止	键盘产生的中断(Ctrl-C)
      3	SIGQUIT	dump	键盘产生的退出
      4	SIGILL	dump	非法指令
      5	SIGTRAP	dump	debug中断
      6	SIGABRT／SIGIOT	dump	异常中止
      7	SIGBUS／SIGEMT	dump	总线异常/EMT指令
      8	SIGFPE	dump	浮点运算溢出
      9	SIGKILL	终止	强制进程终止
      10	SIGUSR1	终止	用户信号,进程可自定义用途
      11	SIGSEGV	dump	非法内存地址引用
      12	SIGUSR2	终止	用户信号，进程可自定义用途
      13	SIGPIPE	终止	向某个没有读取的管道中写入数据
      14	SIGALRM	终止	时钟中断(闹钟)
      15	SIGTERM	终止	进程终止
      16	SIGSTKFLT	终止	协处理器栈错误
      17	SIGCHLD	忽略	子进程退出或中断
      18	SIGCONT	继续	如进程停止状态则开始运行
      19	SIGSTOP	停止	停止进程运行
      20	SIGSTP	停止	键盘产生的停止
      21	SIGTTIN	停止	后台进程请求输入
      22	SIGTTOU	停止	后台进程请求输出
      23	SIGURG	忽略	socket发生紧急情况
      24	SIGXCPU	dump	CPU时间限制被打破
      25	SIGXFSZ	dump	文件大小限制被打破
      26	SIGVTALRM	终止	虚拟定时时钟
      27	SIGPROF	终止	profile timer clock
      28	SIGWINCH	忽略	窗口尺寸调整
      29	SIGIO/SIGPOLL	终止	I/O可用
      30	SIGPWR	终止	电源异常
      31	SIGSYS／SYSUNUSED	dump	系统调用异常
 *
 * ------------------------------------------------------------------------
 */

#include "postgres.h"

#include "libpq/pqsignal.h"


/* Global variables */
sigset_t	UnBlockSig,
			BlockSig,
			StartupBlockSig;


/*
 * Initialize BlockSig, UnBlockSig, and StartupBlockSig.
 *
 * BlockSig is the set of signals to block when we are trying to block
 * signals.  This includes all signals we normally expect to get, but NOT
 * signals that should never be turned off.
 *
 * StartupBlockSig is the set of signals to block during startup packet
 * collection; it's essentially BlockSig minus SIGTERM, SIGQUIT, SIGALRM.
 *
 * UnBlockSig is the set of signals to block when we don't want to block
 * signals.
 * 创建三个信号集(sigset_t 类型)
 *  UnBlockSig：所有比特全为0，不屏蔽任何信号
 *  BlockSig：除SIGTRAP、SIGABRT、SIGILL、SIGFPE、SIGSEGV、SIGBUS、SIGSYS
 *      SIGCONT 信号外，屏蔽所有信号
 *  StartupBlockSig：基本同BlockSig，又放开SIGQUIT、SIGTERM、SIGALRM 信号，
 * pqinitmask 只是创建三个信号集，并未将任何一个变量生效
 */
void
pqinitmask(void)
{
	sigemptyset(&UnBlockSig);

	/* Note: InitializeLatchSupport() modifies UnBlockSig. */

	/* First set all signals, then clear some. */
	sigfillset(&BlockSig);
	sigfillset(&StartupBlockSig);

	/*
	 * Unmark those signals that should never be blocked. Some of these signal
	 * names don't exist on all platforms.  Most do, but might as well ifdef
	 * them all for consistency...
	 */
#ifdef SIGTRAP
	sigdelset(&BlockSig, SIGTRAP);
	sigdelset(&StartupBlockSig, SIGTRAP);
#endif
#ifdef SIGABRT
	sigdelset(&BlockSig, SIGABRT);
	sigdelset(&StartupBlockSig, SIGABRT);
#endif
#ifdef SIGILL
	sigdelset(&BlockSig, SIGILL);
	sigdelset(&StartupBlockSig, SIGILL);
#endif
#ifdef SIGFPE
	sigdelset(&BlockSig, SIGFPE);
	sigdelset(&StartupBlockSig, SIGFPE);
#endif
#ifdef SIGSEGV
	sigdelset(&BlockSig, SIGSEGV);
	sigdelset(&StartupBlockSig, SIGSEGV);
#endif
#ifdef SIGBUS
	sigdelset(&BlockSig, SIGBUS);
	sigdelset(&StartupBlockSig, SIGBUS);
#endif
#ifdef SIGSYS
	sigdelset(&BlockSig, SIGSYS);
	sigdelset(&StartupBlockSig, SIGSYS);
#endif
#ifdef SIGCONT
	sigdelset(&BlockSig, SIGCONT);
	sigdelset(&StartupBlockSig, SIGCONT);
#endif

/* Signals unique to startup */
#ifdef SIGQUIT
	sigdelset(&StartupBlockSig, SIGQUIT);
#endif
#ifdef SIGTERM
	sigdelset(&StartupBlockSig, SIGTERM);
#endif
#ifdef SIGALRM
	sigdelset(&StartupBlockSig, SIGALRM);
#endif
}

/*
 * Set up a postmaster signal handler for signal "signo"
 *
 * Returns the previous handler.
 *
 * This is used only in the postmaster, which has its own odd approach to
 * signal handling.  For signals with handlers, we block all signals for the
 * duration of signal handler execution.  We also do not set the SA_RESTART
 * flag; this should be safe given the tiny range of code in which the
 * postmaster ever unblocks signals.
 *
 * pqinitmask() must have been invoked previously.
 *
 * On Windows, this function is just an alias for pqsignal()
 * (and note that it's calling the code in src/backend/port/win32/signal.c,
 * not src/port/pqsignal.c).  On that platform, the postmaster's signal
 * handlers still have to block signals for themselves.
 */
pqsigfunc
pqsignal_pm(int signo, pqsigfunc func)
{
#ifndef WIN32
	struct sigaction act,
				oact;

	act.sa_handler = func;  // 设置信号处理程序
	if (func == SIG_IGN || func == SIG_DFL)
	{
		/* in these cases, act the same as pqsignal() */
		sigemptyset(&act.sa_mask);
		act.sa_flags = SA_RESTART;
	}
	else
	{
		act.sa_mask = BlockSig;
        // 是否 restart，信号处理时被中断后重新唤醒，无虚 restart，接着执行即可
		act.sa_flags = 0;
	}
#ifdef SA_NOCLDSTOP
	if (signo == SIGCHLD)
		act.sa_flags |= SA_NOCLDSTOP;
#endif
	if (sigaction(signo, &act, &oact) < 0)
		return SIG_ERR;
	return oact.sa_handler;
#else							/* WIN32 */
	return pqsignal(signo, func);
#endif
}
