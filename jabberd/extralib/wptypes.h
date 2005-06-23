
#ifndef FALSE
#define FALSE		    0
#endif

#ifndef TRUE
#define TRUE		    1
#endif

#ifndef max
#define max(a,b)	    (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)	    (((a) < (b)) ? (a) : (b))
#endif

typedef unsigned char BOOL;
typedef unsigned char BYTE;
typedef unsigned char UCHAR;

typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef short	       SHORT;

typedef int	      SOCKET;
typedef int	      INT;
typedef unsigned int  UINT;

#ifdef LONG64B
typedef long INT64;
typedef unsigned long UINT64;
#define INT64FORMAT "%ld"
#define UINT64FORMAT "%lu"
#else
typedef long long INT64;
typedef unsigned long long UINT64;
#define INT64FORMAT "%lld"
#define UINT64FORMAT "%llu"
#endif
