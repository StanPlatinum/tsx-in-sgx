#include <stdarg.h>
#include <stdio.h>

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
		_xend();
	} else {
		_xabort(0);
		PrintDebugInfo("xabort works...\n");
	}

	PrintDebugInfo("--- test 2 ---\n");
        if ((status = _xbegin()) == _XBEGIN_STARTED) {
                if (_xtest()) {
					PrintDebugInfo("in _xtest()\n");
                    _xabort(2);
				}
                _xend();
        } else
                PrintDebugInfo("aborted %x, %d\n", status, _XABORT_CODE(status));


}
