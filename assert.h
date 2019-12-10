#undef assert
#define assert(x) do { if (!(x)) __builtin_trap(); } while(0)
//#define assert(x)
