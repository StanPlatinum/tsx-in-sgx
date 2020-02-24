#ifndef HDR 
#define HDR "rtm.h"
#endif
#include HDR
#include <stdio.h>

/* Requires TSX support in the CPU */

int main(void)
{
	int status;
	int nonce = 0;
	int mutex = 0;

	int xtest_status;

	int i = 0;
	int *g;
	g = &i;

	/* test 0 */
	printf("---test0---\n");
	while(1){ 	// keep trying
		int status = _xbegin(); 	// set status = -1 and start transaction
		printf("status now: %d\n", status);

		if (status == _XBEGIN_STARTED) { // status == XBEGIN_STARTED == -1
			printf("entering into test0's trans...\n");
			(*g)++;	 // non atomic increment of shared global variable
			_xend(); // end transaction
			break;	 // break on success
		} else { 
			printf("aborts..., *g = %d\n", (*g));	 // code here executed if transaction aborts
		} 
	}

	/* test 1 */
	status = 0;
	printf("---test1---\n");
	if ((status = _xbegin()) == _XBEGIN_STARTED) {
		printf("transaction started...\n");
		if (_xtest())
			xtest_status = _xtest();
		printf("testing if in a trans..., xtest_status: %d\n", xtest_status);
		_xabort(2);
		_xend();
	} else
		printf("aborted %x, %d\n", status, _XABORT_CODE(status));

	/* test 2 */
	status = 0;
	printf("---test2---\n");
	if ((status = _xbegin()) == _XBEGIN_STARTED) {
		//if(_xbegin() == -1) {
		printf("transaction begins...\n");
		mutex = 1;
	} else {
		// #pragma omp critical
		printf("mutex: %d\n", mutex);
		printf("nonce: %d\n", nonce);
		_xabort(0);
	}
	_xend();

	return 0;
	}
