/* GCC 新特性最小测试: _Static_assert + float/double + struct 位字段
 * 编译: gcc /Program/gcc/gcc_test.c -o /Program/gcc/gcc_test.efs -T 0x800000
 */

_Static_assert(1, "always true");
_Static_assert(4 == 4);
_Static_assert(sizeof(int) == 4, "int must be 4 bytes");

struct Flags {
    unsigned f1 : 3;
    unsigned f2 : 5;
    unsigned f3 : 8;
    int      normal;
};

_Static_assert(sizeof(struct Flags) == 8, "Flags size mismatch");

float  g_f = 3.14f;
double g_d = 2.71828e0;

int main(void) {
    _Static_assert(sizeof(double) == 8, "block-scope static assert");

    int a = (int)g_f;
    double b = (double)a + g_d;
    int c = (int)(b * g_f);
    if (a + c == 0) return 1;

    if (g_f < 0.0f) return 2;
    if (g_d > 0.0) return 0;

    struct Flags fl;
    fl.f1 = 7;
    fl.f2 = 31;
    fl.f3 = 255;
    fl.f1 += 1;
    fl.f2++;
    fl.normal = fl.f3 + fl.f1;
    if (fl.normal <= 0) return 3;

    return (int)(g_d + g_f + fl.f2 + a);
}
