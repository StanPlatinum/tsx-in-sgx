#include <stdarg.h>
#include <stdio.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include <atomic>
#include <iostream>
#include <thread>

#include <immintrin.h>

#include "Enclave.h"
#include "Enclave_t.h"
#include "rtm.h"

void PrintDebugInfo(const char *fmt, ...)
{
	char buf[BUFSIZ] = {'\0'};
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);
	Ocall_PrintString(buf);
}
void rtm_test(){
	int status;
	int nonce = 0;
	int mutex = 0;

	PrintDebugInfo("--- test 1 ---\n");
	if ((status = _xbegin()) == _XBEGIN_STARTED) {
		mutex = 1;
	} else {
		_xabort(0);
		PrintDebugInfo("xabort works...\n");
	}
	_xend();

	PrintDebugInfo("--- test 2 ---\n");
        if ((status = _xbegin()) == _XBEGIN_STARTED) {
                if (_xtest())
                        _xabort(2);
                _xend();
        } else
                PrintDebugInfo("aborted %x, %d\n", status, _XABORT_CODE(status));
}

void single_thread()
{
	// to-do
}

std::ostream &
operator<<(std::ostream &os, const CacheLine &cl)
{
	os << cl.c[0];
	return os;
}

// This function must be ran in transaction context
static inline void trx_func(unsigned long thr_id, unsigned long trx_sz, int trx_count, int overlap)
{
	for (int c = 0; c < trx_count; c++)
		for (unsigned i = 0; i < trx_sz; ++i) {
			unsigned long shift = thr_id * trx_sz + i
					      - overlap * thr_id;
			debit[shift] += 1;
			credit[shift] += -1;
		}
}

static void warm_and_clear_memory()
{
	aborts = 0;
	retries = 0;

	memset(debit, 0, sizeof(debit));
	memset(credit, 0, sizeof(credit));
}

static void check_consistency(unsigned trx_buf_sz)
{
	for (unsigned i = 0; i < trx_buf_sz; ++i)
		if (debit[i] + credit[i])
			std::cout << "!!! INCONSISTENCY at " << i
				  << ": debit=" << debit[i]
				  << " credit=" << credit[i] << std::endl;
}

static void execute_spinlock_trx(unsigned long thr_id, unsigned long trx_sz, int trx_count, int overlap)
{
	pthread_spin_lock(&spin_l);

	trx_func(thr_id, trx_sz, trx_count, overlap);

	pthread_spin_unlock(&spin_l);
}

// Transaction.
// Reruns transaction specified number of times before abort.
// @return false if the transaction is aborted and true otherwise.
static void execute_short_trx(unsigned long trx_id, unsigned long trx_sz, int trx_count, int overlap)
{
	int abrt = 0;
	while (1) {
		unsigned status = _xbegin();

		if (__builtin_expect(status == _XBEGIN_STARTED, 1)) {
			// we're in transactional context

			// Hacky check whether spinlock is locked.
			// See glibc/nptl/sysdeps/x86_64/pthread_spin_unlock.S
			if (__builtin_expect((int)spin_l != 1, 0))
				_xabort(_ABORT_LOCK_BUSY);

			trx_func(trx_id, trx_sz, trx_count, overlap);

			_xend();

			return;
		}

		ABRT_COUNT(_XA_RETRY, status);
		ABRT_COUNT(_XA_EXPLICIT, status);
		ABRT_COUNT(_XA_CONFLICT, status);
		ABRT_COUNT(_XA_CAPACITY, status);

		if (__builtin_expect(!(status & _XABORT_RETRY), 0)) {
			++_aborts;

			// "Randomized" backoffs as suggested by Andreas Kleen.
			// See http://software.intel.com/en-us/forums/topic/488911
			if (++abrt == abrt_fallback[af]) {
				af = (af + 1) % (sizeof(abrt_fallback)
						 / sizeof(*abrt_fallback));
				break;
			}

			// Backoff if the abort was neither due to conflict with
			// other transaction nor acquired spin lock.
			if (!((status & _XABORT_CONFLICT)
			      || ((status & _XABORT_EXPLICIT)
				  && _XABORT_CODE(status) != _ABORT_LOCK_BUSY)))
				break;

			if ((status & _XABORT_EXPLICIT)
			    && _XABORT_CODE(status) != _ABORT_LOCK_BUSY)
			{
				// Whait while spin lock is released before
				// restart transaction.
				while ((int)spin_l != 1)
					_mm_pause();
				continue;
			}
		}

		++_retries;

		_mm_pause();
	}

	// fallback to spinlock.
	execute_spinlock_trx(trx_id, trx_sz, trx_count, overlap);
}

struct Thr {
	unsigned long trx_sz;
	unsigned long iter;
	int thr_num, thr_id;
	int trx_count, overlap;
	Sync sync;

	Thr(int trx_sz, int trx_count, int interleace, int iter, int thr_num,
	    int thr_id, Sync sync)
		: trx_sz(trx_sz), trx_count(trx_count), overlap(overlap),
		iter(iter), thr_num(thr_num), thr_id(thr_id), sync(sync)
	{
		assert(thr_id < CORES);
		assert(thr_id < thr_num);
		assert(thr_num * trx_sz <= TRX_BUF_SZ_MAX);
		assert(overlap <= trx_sz);
	}

	Thr& operator()()
	{
		_aborts = _retries = 0;

		set_affinity();

		for (unsigned long i = 0; i < iter; ++i) {
			switch (sync) {
			case Sync::TSX:
				execute_short_trx(thr_id, trx_sz, trx_count,
						  overlap);
				break;
			case Sync::SpinLock:
				execute_spinlock_trx(thr_id, trx_sz, trx_count,
						     overlap);
				break;
			default:
				abort();
			}
		}

		// merge statistics
		aborts += _aborts;
		retries += _retries;
#ifdef ABORT_COUNT
		pthread_spin_lock(&spin_l);
		std::cout << "\t\texplicit abrt: " << _abrt[_XA_EXPLICIT]
			  << "\n\t\tretry abrt: " << _abrt[_XA_RETRY]
			  << "\n\t\tconflict abrt: " << _abrt[_XA_CONFLICT]
			  << "\n\t\tcapacity abrt: " << _abrt[_XA_CAPACITY]
			  << std::endl;
		pthread_spin_unlock(&spin_l);
#endif
		return *this;
	}

private:
	// Sets affinity for i7-4650U (dual core with hyper threading).
	// This processor has 4 virtual processors (visible to Linux):
	// cpus 0 and 2 are threads of 1st core and cpus 1 and 3 are threads
	// of 2nd core.
	// So set affinity to cpus 0 and 1 if thr_num == 2.
	void
	set_affinity()
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(thr_id, &cpuset);
		int r = pthread_setaffinity_np(pthread_self(), sizeof(cpuset),
					       &cpuset);
		assert(!r);
	}
};

static inline unsigned long tv_to_ms(const struct timeval &tv)
{
	return ((unsigned long)tv.tv_sec * 1000000 + tv.tv_usec) / 1000;
}

static void run_test(int thr_num, int trx_sz, int trx_count, int overlap, int iter, Sync sync)
{
	struct timeval tv0, tv1;
	std::thread thr[thr_num];

	warm_and_clear_memory();

	int r = gettimeofday(&tv0, NULL);
	assert(!r);

	for (int i = 0; i < thr_num; ++i)
		thr[i] = std::thread(Thr(trx_sz, trx_count, overlap, iter,
					 thr_num, i, sync));

	for (auto &t : thr)
		t.join();

	r = gettimeofday(&tv1, NULL);
	assert(!r);

	check_consistency(thr_num * trx_sz);

	std::cout << "thr=" << thr_num << "\ttrx_sz=" << trx_sz
		<< "\ttrx_count=" << trx_count << "\toverlap=" << overlap
		<< "\titer=" << iter
		<< "\ttime=" << (tv_to_ms(tv1) - tv_to_ms(tv0)) << "ms"
		<< "\taborts=" << aborts.load()
		<< "(" << (aborts.load() * 100 / (iter * thr_num)) << "%)"
		<< "\tretries=" << retries.load()
		<< std::endl;
}
