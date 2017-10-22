#ifndef RTEMS_MINIMAL_VERSION_H
#define RTEMS_MINIMAL_VERSION_H

/* Macro to detect RTEMS version */
#include <rtems.h>
#include <rtems/system.h>

#define RTEMS_ISMINVERSION(ma,mi,re) \
	(    __RTEMS_MAJOR__  > (ma)	\
	 || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__  > (mi))	\
	 || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__ == (mi) && __RTEMS_REVISION__ >= (re)) \
    )

#endif
