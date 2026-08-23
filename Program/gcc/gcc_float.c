/* Phase 2: 测试全局变量 (float/double/int) + cast + 运行输出 */
float  g_f = 3.14f;
double g_d = 2.71828;
int    g_i = 42;

int main(void) {
    print("gcc_float running\n");
    int a = (int)g_f;
    double b = (double)a + g_d;
    if (b > 0.0) print("b > 0\n");
    return g_i;
}
