#include <stdint.h>

struct tail_ok {
	int32_t a;
	int32_t b;
};

struct tail_bad {
	int32_t a;
	int32_t b;
};

struct rename_ok {
	int32_t old_name;
	int32_t keep;
};

struct ptr_ok {
	int32_t *p;
};

struct array_bad {
	int32_t arr[4];
};

struct bitfield_bad {
	unsigned int a : 3;
	unsigned int b : 5;
};

enum enum_ok {
	ENUM_OK_A = 0,
	ENUM_OK_B = 1,
};

struct inner_bad {
	int32_t x;
	int32_t y;
};

struct embed_bad {
	struct inner_bad in;
	int32_t tail;
};

struct signed_bad {
	int32_t x;
};

volatile struct tail_ok g_tail_ok;
volatile struct tail_bad g_tail_bad;
volatile struct rename_ok g_rename_ok;
volatile struct ptr_ok g_ptr_ok;
volatile struct array_bad g_array_bad;
volatile struct bitfield_bad g_bitfield_bad;
volatile enum enum_ok g_enum_ok;
volatile struct embed_bad g_embed_bad;
volatile struct signed_bad g_signed_bad;

int
main(void)
{
	return 0;
}
