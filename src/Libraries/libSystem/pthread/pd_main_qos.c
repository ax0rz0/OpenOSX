#include <stdint.h>
#include <pthread/priority_private.h>

extern uint32_t _main_qos;

void
_pthread_set_main_qos(pthread_priority_t qos)
{
	_main_qos = (uint32_t)qos;
}
