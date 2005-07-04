
extern int debug_flag;
/*
 *   debug functions
 */
extern void debug_log(const char *msgfmt, ...);

#define S(s) str(s)
#define str(s) #s
#define SAFE_DEBUG_STR(str) ((str)?(str):"null")

#ifdef NODEBUG
#define log_debug(arg...)
#else
#define log_debug(format,arg...) if(debug_flag) debug_log(__FILE__ ":" S(__LINE__) " \t " format, ## arg );
#endif
