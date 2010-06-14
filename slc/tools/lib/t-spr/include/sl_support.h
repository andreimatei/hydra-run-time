
#define sl_proccall(Fun, ...) do { sl_create(,,,,,,,Fun, ## __VA_ARGS__); sl_sync(); } while(0)

[[#]]ifndef NULL
[[#]]define NULL 0
[[#]]endif

#ifdef __SL_EXTRA_INCLUDE
#include __SL_EXTRA_INCLUDE
#endif

