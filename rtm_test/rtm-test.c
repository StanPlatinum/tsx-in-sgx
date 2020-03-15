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

	int xtest_status = 0;

	int i = 0;
	int *g;
	g = &i;

	/* test 0 */
	printf("---test0---\n");
	while(i < 100){ 	// keep trying 100 times
		int status = _xbegin(); 	// set status = -1 and start transaction
		if (status == _XBEGIN_STARTED) { // status == XBEGIN_STARTED == -1
			//printf("entering into test0's trans...\n");
			(*g)++;	 // non atomic increment of shared global variable
			_xend(); // end transaction
			//break;	 // break on success
		} else { 
			printf("aborts..., *g = %d\n", (*g));	 // code here executed if transaction aborts
		} 
	}

	/* test 1 */
	status = 0;
	printf("---test1---\n");
	if ((status = _xbegin()) == _XBEGIN_STARTED) {
		if (_xtest())
			xtest_status = _xtest();
		//Weijie: try to use xabort
		//_xabort(2);
		_xend();
	} else
		printf("aborted %x, %d\n", status, _XABORT_CODE(status));
	
	//Weijie: check the xabort code after trans
	//Weijie: turns out if no _xabort code was specified, the default value would be 255
	printf("aborted %x, %d\n", status, _XABORT_CODE(status));
	printf("testing if in a trans..., xtest_status: %d\n", xtest_status);

	/* test 2 */
	status = 0;
	printf("---test2---\n");
	if ((status = _xbegin()) == _XBEGIN_STARTED) {
		mutex = 1;
		//printf("use printf to abort ...\n");
		_xend();
	} else {
		// #pragma omp critical
		printf("mutex: %d\n", mutex);
		printf("nonce: %d\n", nonce);
	}

	printf("test finished\n");
	return 0;
}
